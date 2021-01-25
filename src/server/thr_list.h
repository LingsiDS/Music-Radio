#ifndef THR_LIST_H__
#define THR_LIST_H__

// #include <pthread.h>
#include "medialib.h"

int thr_list_create(struct mlib_listentry_st*, int size);
int thr_list_destory();

#endif