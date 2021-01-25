#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <unistd.h>


#include "thr_list.h"
#include "server_conf.h"
#include "medialib.h"
#include "proto.h"

static pthread_t tid_list;  //节目单线程id
static int nr_list_ent;     //节目单总数量
static struct mlib_listentry_st *list_ent;


//节目单工作线程，每隔1秒往组播地址发送节目单
static void *thr_list(void *p) {
    int totalsize;  //发送数据的总大小
    int size;
    int ret;
    struct msg_list_st *entlistp;//将要发送的数据组织到entlistp内
    struct msg_listentry_st *entryp;
    totalsize = sizeof(chnid_t);//节目单频道id


    for (int i = 0; i < nr_list_ent; i++) {
        totalsize += sizeof(struct msg_listentry_st) + strlen(list_ent[i].desc);
    }

    entlistp = malloc(totalsize);
    if (entlistp == NULL) {
        syslog(LOG_ERR, "malloc(): %s", strerror(errno));
        exit(1);
    }

    entlistp->chnid = LIST_CHNID;//以下代码填充好entlistp
    entryp = entlistp->entry;

    for (int i = 0; i < nr_list_ent; i++) {
        size = sizeof(struct msg_listentry_st) + strlen(list_ent[i].desc);
        entryp->chnid = list_ent[i].chnid;
        entryp->len = htons(size);
        strcpy(entryp->desc, list_ent[i].desc);
        entryp = (void *)((char *)entryp + size);
    }

    //----------------------------这一段可能有问题---------------------------------//
    char ipstr[30];
    inet_ntop(AF_INET, &serv_addr.sin_addr.s_addr, ipstr, 30);
    while (1) {
        syslog(LOG_INFO, "thr_list serv_addr: %d\n", ipstr);
        ret = sendto(serv_sd, entlistp, totalsize, 0, (void *)&serv_addr, sizeof(serv_addr));
        syslog(LOG_INFO, "thr_list list msg size: %d\n", totalsize);  

        if (ret < 0) {
            syslog(LOG_WARNING, "sendto(serv_sd, entlistp...%s", strerror(errno));
        } 
        syslog(LOG_INFO, "before sleep()\n"); 
        sleep(1);
        syslog(LOG_INFO, "after sleep()\n"); 
    }
}

//创建节目单线程
int thr_list_create(struct mlib_listentry_st *listp, int nr_ent) {
    int err;
    //保存数据
    list_ent = listp;//listp 为struct mlib_listentry_st的数组，是待发送的所有节目单数据，将该数据保存到全局变量list_ent
    nr_list_ent = nr_ent;

    err = pthread_create(&tid_list, NULL, thr_list, NULL);
    if (err) {
        syslog(LOG_ERR, "pthread_create(): %s", strerror(errno));
        return -1;
    }
    return 0;
}

//节目单线程销毁
int thr_list_destory(void) {
    pthread_cancel(tid_list);
    pthread_join(tid_list, NULL);
    return 0;
}