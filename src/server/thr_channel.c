#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <pthread.h>

#include "medialib.h"
#include "thr_channel.h"
#include "thr_list.h"
#include "../include/proto.h"
#include "server_conf.h"

static int tid_nextpos = 0;

struct thr_channel_ent_st {
    chnid_t chnid;  //频道号
    pthread_t tid;  //负责该频道的线程
};

struct thr_channel_ent_st thr_channel[CHNNR];


static void *thr_channel_snder(void *ptr) {
    //sbufp存放当前频道要发送的数据，以下代码主要任务为组织好发送数据，并且向指定地址发送数据
    struct msg_channel_st *sbufp;
    int len;
    
    struct mlib_listentry_st *ent = ptr;
    sbufp = malloc(MSG_CHANNEL_MAX);
    if (sbufp == NULL) {
        syslog(LOG_ERR, "malloc(): %s", strerror(errno));
        exit(1);
    }
    sbufp->chnid = ent->chnid;

    while (1) {
        //len = mlib_readchn(ent->chnid, sbufp->data, MAX_DATA);
        syslog(LOG_DEBUG, "ready to call mlib_readchn()\n");
        len = mlib_readcnt(ent->chnid, sbufp->data, 128 * 1024 / 8);

        syslog(LOG_DEBUG, "mlib_readchn() len: %d", len);
        if (sendto(serv_sd, sbufp, len, 0, (void *)&serv_addr, sizeof(serv_addr)) < 0) {
            syslog(LOG_ERR, "thr_channel(%d): sendto(): %s", ent->chnid, strerror(errno));
            break;
        }
        syslog(LOG_DEBUG, "thr_channel send msg success, msg size = %d\n", len);
        sched_yield();
    }
    pthread_exit(NULL);
}


//创建一个频道线程
int thr_channel_create(struct mlib_listentry_st *ptr) {//ptr内包含当前频道的chnid和desc，只使用chnid
    int err;
    err = pthread_create(&thr_channel[tid_nextpos].tid, NULL, thr_channel_snder, ptr);//创建线程
    if (err) {
        syslog(LOG_WARNING, "pthread_create(): %s", strerror(err));
        return -err;
    }
    thr_channel[tid_nextpos].chnid = ptr->chnid;//创建线程，并记录线程tid和频道id对应信息
    tid_nextpos++;
    return 0;
} 


//销毁一个频道线程
int thr_channel_destroy(struct mlib_listentry_st *ptr) {
    for (int i = 0; i < CHNNR; i++) {
        if (thr_channel[i].chnid == ptr->chnid) {//找到了要销毁的频道
            if (pthread_cancel(thr_channel[i].tid) < 0) {//取消线程
                syslog(LOG_ERR, "pthread_cancel(): thr thread of channel %d", ptr->chnid);
                return -ESRCH;
            }
        }
        pthread_join(thr_channel[i].tid, NULL);//join线程
        thr_channel[i].chnid = -1;
        return 0;
    }
}

//销毁所有频道线程
int thr_channel_destoryall(void) {
    for (int i = 0; i < CHNNR; i++) {
        if (thr_channel[i].chnid > 0) {
            if (pthread_cancel(thr_channel[i].tid) < 0) {
                syslog(LOG_ERR, "pthread_cancel():thr thread of channel:%ld", thr_channel[i].tid);
                return -ESRCH;
            }
            pthread_join(thr_channel[i].tid, NULL);
            thr_channel[i].chnid = -1;
        }
    }
}