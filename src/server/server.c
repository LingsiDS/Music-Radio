// #define _POSIX_C_SOURCE
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


#include "server_conf.h"
#include "../include/proto.h"// #include <proto.h>  //make file CFLAGS+=-I../include/ 
#include "medialib.h"
#include "thr_channel.h"
#include "thr_list.h"

/*  命令行参数：
        -M 指定多播组
        -P 指定接受端口
        -F 前台运行
        -D 指定媒体库位置
        -I 指定网络设备（网卡）
        -H 显示帮组
        
*/
static void print_help() {
    printf("-M      指定多播组\n");
    printf("-P      指定接受端口\n");
    printf("-F      前台运行\n");
    printf("-D      指定媒体库位置\n");
    printf("-I      指定网络设备（网卡）\n");
    printf("-H      显示帮组\n");
}


struct server_conf_st serv_conf = {DEFAULT_PORT, DEFAULT_MGROUP,\
                                DEFAULT_MEDIA_DIR, RUN_DAEMON, DEFAULT_IF};
int serv_sd = 0;
struct sockaddr_in serv_addr;



static int demaonize() {
    pid_t pid = fork();
    if (pid < 0) {
        // perror("fork()");
        syslog(LOG_ERR, "fork(): %s", strerror(errno));
        return -1;
    }

    if (pid > 0)    //父进程 
        exit(0);

    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        // perror("open()");
        syslog(LOG_WARNING, "open(): %s", strerror(errno));
        return -2;
    } else {
        dup2(fd, 0);//关闭文件描述符
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    }

    setsid();

    chdir("/"); //更改工作目录
    umask(0);
    return 0;   //成功将当前进程变为守护进程
}


static struct mlib_listentry_st *list;//list相当于 struct mlib_listentry_st list[list_size];

static void daemon_exit(int s) {
    //守护进程中，来不及做的释放资源动作放入该函数中

    thr_list_destory();
    thr_channel_destoryall();
    mlib_freechnlist(list);

    syslog(LOG_WARNING, "signal-%d caught, exit now.", s);
    closelog();
    exit(0);
}


// struct sigaction {
//     void     (*sa_handler)(int);
//     void     (*sa_sigaction)(int, siginfo_t *, void *);
//     sigset_t   sa_mask;
//     int        sa_flags;
//     void     (*sa_restorer)(void);
// };

static int socket_init() {
    //建立套接字
    serv_sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (serv_sd < 0) {
        syslog(LOG_ERR, "socket(): %s", strerror(errno));
        exit(1);
    }

    //设置套接字选项
    struct ip_mreqn mreq;
    int mreq_size = sizeof(mreq);
    inet_pton(AF_INET, serv_conf.mgroup, &mreq.imr_multiaddr);
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);
    mreq.imr_ifindex = if_nametoindex(serv_conf.ifname);
    if (setsockopt(serv_sd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) < 0) {
        syslog(LOG_ERR, "setsockopt(IP_MULTICAST_IF): %s", strerror(errno));
        exit(1);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(serv_conf.rcvport));
    inet_pton(AF_INET, serv_conf.mgroup, &serv_addr.sin_addr.s_addr);
    
    return 0;
}

int main (int argc, char *argv[]) {

    struct sigaction sa;
    sa.sa_handler = daemon_exit;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGTERM);
    sigaddset(&sa.sa_mask, SIGQUIT);
    sigaddset(&sa.sa_mask, SIGINT);

    sigaction(SIGTERM, &sa, NULL);    
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);    


    //命令行分析
    int c;
    while (1) {
        c = getopt(argc, argv, "M:P:FD:I:H");
        if (c < 0) 
            break;
        switch (c)
        {
        case 'M':
            serv_conf.mgroup = optarg;
            break;
        case 'P':
            serv_conf.rcvport = optarg;
            break;
        case 'F':
            serv_conf.runmod = RUN_FRONT;
            break;
        case 'D':
            serv_conf.media_dir = optarg;
            break;
        case 'I':
            serv_conf.ifname = optarg;
            break;
        case 'H':
            print_help();
            exit(0);
            break;
        default:
            abort();
            break;
        }
    }
    
    openlog("mediaRadio_serv", LOG_PID | LOG_PERROR, LOG_DAEMON);//打开系统日志
    //守护进程实现
    if (serv_conf.runmod == RUN_DAEMON) {
        if (demaonize() != 0) {//将当前进程转为守护进程
            exit(1);
        }
    } else if (serv_conf.runmod == RUN_FRONT) {
        //server端前台运行，当前不需要做任何工作
    } else {
        // fprintf(stderr, "RUNMOD EINVAL\n");
        syslog(LOG_ERR, "EINVAL RUNMOD");
        exit(1);
    }
        

    //socket初始化
    socket_init();

    //获取频道信息
    //struct mlib_listentry_st *list;//list相当于 struct mlib_listentry_st list[list_size];
    int list_size = 0;
    int err = mlib_getchnlist(&list, &list_size);
    if (err) {

    }

    //创建节目单线程
    thr_list_create(list, list_size);    

    //创建频道线程
    int i;
    for (i = 0; i < list_size; i++) {
        syslog(LOG_DEBUG, "pthread_create, chnid = %d\n", (list + i)->chnid);
        thr_channel_create(list + i);//每个频道创建一个线程，每个线程发送一个频道的数据
        // if (error)
    }
    syslog(LOG_DEBUG, "%d channel thread created", i);


    while (1) {
        pause();
    }

    exit(0);
}