#ifndef PROTO_H__
#define PROTO_H__

#include "mytype.h"

#define DEFAULT_MGROUP  "224.2.2.2"  //默认多播组
#define DEFAULT_PORT    "7777"       //默认端口号

#define CHNNR   100     //频道最大数量
#define LIST_CHNID  0   //节目列表的频道ID

#define MIN_CHNID   1                       //最小的频道ID
#define MAX_CHNID   (MIN_CHNID + CHNNR - 1) //最大的频道ID




#define MSG_CHANNEL_MAX     (65536 - 20 - 8)            //UDP传递一个频道包的最大长度
#define MAX_DATA    (MSG_CHNNEL_MAX - sizeof(chnid_t))  //频道包的数据部分的最大长度，即变长数组的最大长度

struct msg_channel_st {  //一个频道对应的信息
    chnid_t chnid;       //对于频道信息，chnid 在[MIN_CHNID, MAX_CHNID]之间
    uint8_t data[1];     //变长数据部分
}__attribute__((packed));




#define MSG_LIST_MAX    (65536 - 20 -8)                 //UDP传递频道单包的最大长度
#define MAX_ENTRY       (MSG_LIST_MAX - sizeof(chnid_t))//频道单包的数据部分的最大长度，即变长数组的最大长度

struct msg_listentry_st {
    chnid_t chnid;      //当前频道的ID
    int len;            //当前这个结构体的整个大小
    uint8_t desc[1];    //当前频道的文字描述信息
}__attribute__((packed));

struct msg_list_st {    //一个频道单对应的信息
    chnid_t chnid;      //对于节目列表，一定是LIST_CHNID
    struct msg_listentry_st entry[1];  //节目列表变长数据部分，节目数量不定
}__attribute__((packed));

#endif