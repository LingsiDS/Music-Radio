#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "mytbf.h"


/*                              令牌桶算法简介
    令牌桶中的每一个令牌都代表一个字节。如果令牌桶中存在令牌，则允许发送流量；而如果令牌桶中不存在令牌，则不允许发送流量。
    大小固定的令牌桶可自行以恒定的速率源源不断地产生令牌。如果令牌不被消耗，或者被消耗的速度小于产生的速度，
令牌就会不断地增多，直到把桶填满。后面再产生的令牌就会从桶中溢出。最后桶中可以保存的最大令牌数永远不会超过桶的大小。

*/
//多线程并发版令牌桶
struct mytbf_st {
    int cps;                //速率，每秒中产生cps个令牌
    int burst;              //桶的上限
    int token;              //令牌数量
    int pos;                //当前临牌桶在令牌桶数组job中的下标
    pthread_mutex_t mut;    //互斥量，防止多线程访问冲突
    pthread_cond_t cond;    //多线程同步
};

static struct mytbf_st *job[MYTBF_MAX]; //全局变量，令牌桶管理结构，注意job是static的，对外只提供mytbf.h声明的函数
static pthread_mutex_t mut_job = PTHREAD_MUTEX_INITIALIZER;//对整个job数组进行互斥
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static pthread_t tid;

static void *thr_alrm(void *p) {//工作线程，每秒钟对所有令牌桶加上一定数量的token，在模块初始化时注册该函数
    while (1) {
        pthread_mutex_lock(&mut_job);
        for (int i = 0; i < MYTBF_MAX; i++) {
            if (job[i] != NULL) {
                pthread_mutex_lock(&job[i]->mut);
                job[i]->token += job[i]->cps;
                if (job[i]->token > job[i]->burst)
                    job[i]->token = job[i]->burst;
                pthread_cond_broadcast(&job[i]->cond);
                pthread_mutex_unlock(&job[i]->mut);
            }
        }
        pthread_mutex_unlock(&mut_job);
        sleep(1);
    }
}

static void module_unload(void) {
    int i;
    pthread_cancel(tid);//结束令牌桶工作线程
    pthread_join(tid, NULL);

    for (int i = 0; i < MYTBF_MAX; i++) 
        free(job[i]);
    return ;
}

static void module_load(void) {
    
    int err = pthread_create(&tid, NULL, thr_alrm, NULL);
    if (err) {
        fprintf(stderr, "pthread_create(): %s\n", strerror(errno));
        exit(1);
    }

    atexit(module_unload);//Register a function to be called when `exit' is called.
}

static int get_free_pos_unlocked(void) {
    for (int i = 0; i < MYTBF_MAX; i++) {
        if (job[i] == NULL)
            return i;
    }
    return -1;
}

mytbf_t *mytbf_init(int cps, int burst) {//令牌桶初始化，cps为每秒读取文件的速率，burst为上限
    
    //pthread_once  is to ensure that a piece of initialization code is executed at most once.
    pthread_once(&init_once, module_load);//第一次执行该函数时，会调用一次module_load
    
    struct mytbf_st *me;
    me = malloc(sizeof(*me));
    if (me == NULL) {
        perror("malloc()");
        return NULL;
    }

    me->cps = cps;
    me->burst = burst;
    me->token = 0;
    pthread_mutex_init(&me->mut, NULL);
    pthread_cond_init(&me->cond, NULL);

    pthread_mutex_lock(&mut_job);//要访问job数组，对job数组加锁
    int pos = get_free_pos_unlocked();
    if (pos < 0) {
        pthread_mutex_unlock(&mut_job);
        free(me);
        return NULL;
    }
    me->pos = pos;
    job[me->pos] = me;
    pthread_mutex_unlock(&mut_job);

    return me;
}

int min (int a, int b) {
    return a <= b ? a : b;
}

int mytbf_fetchtoken(mytbf_t *ptr, int count) {
    struct mytbf_st *me = ptr;

    pthread_mutex_lock(&me->mut);
    
    while (me->token <= 0)
        pthread_cond_wait(&me->cond, &me->mut);//如果没有token，则等待
    
    int n = min(me->token, count);
    me->token -= n;

    pthread_mutex_unlock(&me->mut);

    return n;
}

int mytbf_returntoken(mytbf_t *ptr, int count) {//归还count个token
    struct mytbf_st *me = ptr;

    pthread_mutex_lock(&me->mut);
    me->token += count;
    if (me->token > me->burst)
        me->token = me->burst;
    pthread_cond_broadcast(&me->cond);
    pthread_mutex_unlock(&me->mut);
    return 0;
}

int mytbf_destory(mytbf_t *ptr) {
    struct mytbf_st *me = ptr;

    pthread_mutex_lock(&mut_job);//对全局job数组加锁，然后释放内存
    job[me->pos] = NULL;
    pthread_mutex_unlock(&mut_job);

    pthread_mutex_destroy(&me->mut);
    pthread_cond_destroy(&me->cond);
    free(me);

    return 0;
}

int mytbf_checktoken(mytbf_t *ptr) {//查看当前令牌桶有多少token
    int token_left = 0;
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&me->mut);
    token_left = me->token;
    pthread_mutex_unlock(&me->mut);
    return token_left;
}