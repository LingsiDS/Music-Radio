#ifndef THR_CHANNEL_H__
#define THR_CHANNEL_H__

#include "medialib.h"

int thr_channel_create(struct mlib_listentry_st *); //创建一个频道线程
int thr_channel_destroy(struct mlib_listentry_st *);//销毁一个频道线程

int thr_channel_destoryall(void);//销毁所有频道线程

#endif