#ifndef MYTBF_H__
#define MYTBF_H__


#define MYTBF_MAX 1024

typedef void mytbf_t;

mytbf_t *mytbf_init(int cps, int burst);//令牌桶初始化，cps为每秒读取文件的速率，burst为上限

int mytbf_fetchtoken(mytbf_t *, int);

int mytbf_returntoken(mytbf_t *, int);

int mytbf_destory(mytbf_t *);

int mytbf_checktoken(mytbf_t *ptr);

#endif