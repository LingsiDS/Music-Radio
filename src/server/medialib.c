#include <stdio.h>
#include <stdlib.h>
#include <glob.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "medialib.h"
#include "mytbf.h"
#include "../include/proto.h"
#include "server_conf.h"


#define PATH_SIZE 1024
#define LINE_BUF_SIZE 1024
#define MP3_BITRATE 256*1024

//该结构包括了描述一个频道的所有信息
struct channel_context_st {
    chnid_t chnid;      //频道ID
    char *desc;         //频道基本描述信息
    glob_t mp3glob;     //当前频道的目录结构（主要是存储了所有mp3文件的文件名）
    int pos;            //正在播放当前频道的第pos首歌，mp3glob.gl_pathv[pos]为歌的文件名，NULL为结束
    int fd;             //正在播放的歌曲的文件描述符
    off_t offset;       //正在播放歌曲的第offset位置
    mytbf_t *tbf;       //对正在播放歌曲进行流量控制
};

//全局变量存放所有频道的数据
struct channel_context_st channel[CHNNR + 1];

//解析给定的频道路径，得到该频道的所有有用的信息返回
static struct channel_context_st *path2entry(const char *path) {//path is something like “~/media/ch1“
    syslog(LOG_INFO, "current path: %s\n", path);
    char pathstr[PATH_SIZE] = {'\0'};
    char linebuf[LINE_BUF_SIZE];

    FILE *fp;
    struct channel_context_st *me;//存储该路径指定频道的所有有用信息
    static chnid_t curr_id = MIN_CHNID;//当前解析到哪一个频道
    strcat(pathstr, path);
    strcat(pathstr, "/desc.txt");
    fp = fopen(pathstr, "r");
    syslog(LOG_INFO, "channel dir:%s\n", pathstr);
    if (fp == NULL) {//没有desc.txt文件
        syslog(LOG_INFO, "%s is not a channel dir(can't finid desc.txt", path);
        return NULL;
    }
    if (fgets(linebuf, LINE_BUF_SIZE, fp) == NULL) {//有desc.txt文件，读取失败，是否有权限？
        syslog(LOG_INFO, "%s is not a channel dir(can't read the desc.txt, Please check permissions)\n", path);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    me = malloc(sizeof(*me));//申请内存空间，下面填充这个结构
    if (me == NULL) {
        syslog(LOG_ERR, "malloc(): %s", strerror(errno));
        return NULL;
    }

    me->tbf = mytbf_init(MP3_BITRATE / 8, MP3_BITRATE / 8 * 5);//初始化令牌桶
    if (me->tbf == NULL) {
        syslog(LOG_ERR, "mytbf_init(): %s", strerror(errno));
        free(me);
        return NULL;
    }
    me->desc = strdup(linebuf); //复制字符串到动态申请的空间，并返回动态申请空间的地址
    
    strncpy(pathstr, path, PATH_SIZE - 1);//warning：strncpy不拷贝最后一个字节，修改最后一个参数修改为PATH_SIZE - 1
    strncat(pathstr, "/*.mp3", PATH_SIZE - 1);//pattern，准备解析所有.mp3文件s
    if (glob(pathstr, 0, NULL, &me->mp3glob) != 0) {
        curr_id++;
        syslog(LOG_ERR, "%s is not a channel dir(can not find mp3 files)", path);
        free(me);
        return NULL;
    }
    me->pos = 0;        //第pos首歌
    me->offset = 0;     //当前歌的播放位置
    me->fd = open(me->mp3glob.gl_pathv[me->pos], O_RDONLY);//打开当前频道的第一个音乐文件？？？
    if (me->fd < 0) {
        syslog(LOG_WARNING, "%s open failed.", me->mp3glob.gl_pathv[me->pos]);
        free(me);
        return NULL;
    }
    me->chnid = curr_id;//频道id
    curr_id++;
    syslog(LOG_INFO, "chnid = %d, desc = %s\n", me->chnid, me->desc);
    return me;
}

//获取频道节目单，函数的两个指针参数都是要填充的返回值
int mlib_getchnlist(struct mlib_listentry_st** result, int *resnum) {
    char path[PATH_SIZE];//存放待解析的媒体库的路径
    glob_t globres;
    int num = 0;
    struct mlib_listentry_st *ptr;
    struct channel_context_st *res;

    for (int i = 0; i < MAX_CHNID + 1; i++) {//0 - 100所有频道id初始化为-1
        channel[i].chnid = -1;
    }

    snprintf(path, PATH_SIZE, "%s/*", serv_conf.media_dir);//生成待解析的路径pattern
    syslog(LOG_DEBUG, "media path = %s\n", path);
    //glob(): find pathnames matching a pattern
    
    int errcode;
    if (errcode = glob(path, 0, NULL, &globres)) {
        syslog(LOG_INFO, "glob(): media path resolve failed. errcode = %d\n", errcode);
        return -1;
    }

    // for (int i = 0; i < globres.gl_pathc; i++) {//没问题
    //     syslog(LOG_DEBUG, "globres: %s\n", globres.gl_pathv[i]);
    // }

    ptr = malloc(sizeof(struct mlib_listentry_st) * globres.gl_pathc);//该路径下可能有非法目录，后续再realloc
    if (ptr == NULL) {
        syslog(LOG_ERR, "malloc(): %s", strerror(errno));
        exit(1);
    }

    for (int i = 0; i < globres.gl_pathc; i++) {
        //globres.gl_pathv[i] is something like “~/media/ch1“
        res = path2entry(globres.gl_pathv[i]);//得到该路径指定频道的所有信息，这块空间为动态分配，使用完应该释放
        if (res != NULL) {
            // syslog
            memcpy(channel + res->chnid, res, sizeof(*res));//将解析结果转存到channel数组
            ptr[num].chnid = res->chnid;//从res中抽取出节目单的信息
            ptr[num].desc  = res->desc;
            num++;
            //-----------------------!!!-------------------------//
            free(res);//这里是不是应该添加释放操作
            //-----------------------!!!-------------------------//
        }
    }

    //真正频道数量为num个，重新申请内存空间，并且回填节目单数组地址
    *result = realloc(ptr, sizeof(struct mlib_listentry_st) * num);
    if (*result == NULL) {
        syslog(LOG_ERR, "realloc() failed,");
    }
    *resnum = num;
    return 0;
}


int mlib_freechnlist(struct mlib_listentry_st *ptr) {
    free(ptr);
    return 0;
}


//chnid频道的第pos首歌已经播放完毕，尝试播放chnid频道的下一首歌
static int open_next(chnid_t chnid) {

    //这里使用循环是防止一首歌打开失败，如果打开失败，继续尝试打开下一首歌
    for (int i = 0; i < channel[chnid].mp3glob.gl_pathc; i++) {
        channel[chnid].pos++;//下一首歌

        if (channel[chnid].pos == channel[chnid].mp3glob.gl_pathc) {//已经是最后一首歌
            //这里可以考虑再次循环播放当前频道的音乐

            close(channel[chnid].fd);//关闭上一首歌的文件描述符
            return -1;//最后一首歌已经打开完毕，结束
            break;
        }
        close(channel[chnid].fd);//关闭上一首歌的文件描述符
        channel[chnid].fd = open(channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], O_RDONLY);//打开下一首歌
 
        if (channel[chnid].fd < 0) {
            syslog(LOG_WARNING, "open(%s): %s", channel[chnid].mp3glob.gl_pathv[chnid], strerror(errno));
        }else {//打开下一首歌成功
            channel[chnid].offset = 0;
            return 0;
        }
    }
    syslog(LOG_ERR, "None of mp3 in channel %d id available.", chnid);
    return -1;//没有歌曲了，不会执行到
}

//从chnid频道读取size个字节到buf中，返回实际读取到的字节数
ssize_t mlib_readcnt(chnid_t chnid, void *buf, size_t size) {
    int tbfsize;
    int len;
    int next_ret = 0;

    syslog(LOG_DEBUG, "ready to call mytbf_fetchtoken(chnid = %d, size = %d)\n", chnid, size);
    tbfsize = mytbf_fetchtoken(channel[chnid].tbf, size);//从当前频道的令牌桶内获得size个token
    syslog(LOG_INFO, "current tbf(): %d", mytbf_checktoken(channel[chnid].tbf));//查看当前令牌桶的token数量

    while (1) {
        //pread, pwrite: read from or write to a file descriptor at a given offset
        len = pread(channel[chnid].fd, buf, tbfsize, channel[chnid].offset);
        if (len < 0) {//正常情况下，len为读取的字节数，len < 0, pread 出错
            //这首歌可能有问题，错误不至于退出，读取下一首歌
            syslog(LOG_WARNING, "media file %s pread(): %s", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], strerror(errno));
            if (open_next(chnid) < 0) //最后一首歌
                break;
        } else if (len == 0) {
            syslog(LOG_DEBUG, "media %s file is over", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos]);
            next_ret = open_next(chnid);
            break;
        } else {//len > 0 成功读取到数据
            channel[chnid].offset += len;//下一次从歌的offset位置继续读取
            syslog(LOG_DEBUG, "epoch: %f", (channel[chnid].offset) / (16*1000*1.024));
            break;
        }
    }

    //remain some token
    if (tbfsize - len > 0)
        mytbf_returntoken(channel[chnid].tbf, tbfsize - len);
    return len;//返回读取到的长度
}