#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
// #include <netinet/in.h>
// #include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <string.h>
#include <errno.h>

#include <proto.h> //CFLAGS+=-I../include/
// #include "include/proto.h"
#include "client.h"


// struct ip_mreqn {   //vscode头文件可能出错了，将加入多播组的结构在这临时定义一个
//     struct in_addr imr_multiaddr; /* IP multicast group
//                                         address */
//     struct in_addr imr_address;   /* IP address of local
//                                         interface */
//     int            imr_ifindex;   /* interface index */
// };


struct client_conf_st client_conf = { DEFAULT_PORT,
        DEFAULT_MGROUP, DEFAULT_PLAYER_CMD};


static void print_help() {
    printf("-P  --port 指定端口号\n");
    printf("-M  --mgroup 指定多播组\n");
    printf("-p  --player 指定播放器\n");
    printf("-H  --helper 显示帮组\n");
}


static ssize_t writen(int fd, const char *buf, size_t n) {//write的封装，像fd中写够buf开始的n个字节
    int bytes = 0;//已经写入的字节数
    while (n > 0) {
        int ret = write(fd, buf + bytes, n);
        if (ret < 0) {
            if (errno = EINTR) 
                continue;
            perror("write()\n");
            return -1;//写入出错
        }
        bytes += ret;//已写入的字节数增加
        n -= ret;    //剩余待写入的字节数减少
    }
    return bytes;
}


int main (int argc, char *argv[]) {

    /*  配置文件的优先级，从低到高
            默认值，配置文件，环境变量，命令行参数
        命令行参数
            printf("-P  --port 指定端口号\n");
            printf("-M  --mgroup 指定多播组\n");
            printf("-p  --player 指定播放器\n");
            printf("-H  --helper 显示帮组\n");
    */

    int arg_idx = 0;
    struct option option_array[] = {{"port", 1, NULL, 'P'},{"mgroup", 1, NULL, 'M'},\
                                    {"player", 1, NULL, 'p'},{"help", 1, NULL, 'H'},\
                                    {NULL, 0, NULL, 0}};
    while (1) {
        int c = getopt_long(argc, argv, "P:M:p:H", option_array, &arg_idx);
        if (c < 0) 
            break;
        switch (c) {
            case 'P':
                client_conf.rcvport = optarg;
                break;
            case 'M':
                client_conf.mgroup = optarg;
            case 'p':
                client_conf.player_cmd = optarg;
            case 'H':
                print_help();
                exit(0);
            default:
                abort();
                break;
        }
    }
    


    int sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0) {
        perror("socket()");
        exit(1);
    }
    
    struct ip_mreqn mreq;
    if (inet_pton(AF_INET, client_conf.mgroup, &mreq.imr_multiaddr) != 1) {
        fprintf(stderr, "inet_pton() failed\n");
    }
    if (inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address) != 1) {
        fprintf(stderr, "inet_pton() failed\n");
    }
    mreq.imr_ifindex = if_nametoindex("eth0");
    
    if (setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt()");
        exit(1);
    }

    int sock_opt_val = 1;
    setsockopt(AF_INET, IPPROTO_IP, IP_MULTICAST_LOOP, &sock_opt_val, sizeof(sock_opt_val));//发送组播时，0表示禁止回送，1表示允许回送


    struct sockaddr_in clnt_addr;
    memset(&clnt_addr, 0, sizeof clnt_addr);
    clnt_addr.sin_family = AF_INET;
    clnt_addr.sin_port = htons(atoi(client_conf.rcvport));
    if (inet_pton(AF_INET, "0.0.0.0", &clnt_addr.sin_addr) != 1) {
        fprintf(stderr, "inet_pton() failed\n");
    }
    
    if (bind(sd, (struct sockaddr*)&clnt_addr, sizeof(clnt_addr)) < 0) {
        perror("bind()");
        exit(1);
    }
    
    int pd[2];
    if (pipe(pd) < 0) {
        perror("pipe()");
        exit(1);
    }

    pid_t pid;
    pid = fork();
    if (pid < 0) {
        perror("fork()");
        exit(1);
    }
    if (pid == 0) {//子进程调用播放器
        close(sd);
        close(pd[1]);
        dup2(pd[0], 0);//关闭0号标准输入，将管道的读端设置为0号文件描述符
        if (pd[0] > 0) //如果管道读端不是标准输入，dup2已经复制了管道读端，将管道原读端关闭
            close(pd[0]);//

        execl("/bin/sh", "sh", "-c", client_conf.player_cmd, (char *)NULL);//使用bash的-c选项，将后面的字符串当做一个命令串执行
        perror("execl()");
        exit(1);   
    }

    //父进程从网络上接受数据，并且通过管道发送给子进程
    struct msg_list_st *msg_list;
    msg_list = (struct msg_list_st *)malloc(MSG_LIST_MAX);//申请一块缓冲区接受频道信息，大小为一个频道单包的最大长度
    if (msg_list == NULL) {
        perror("malloc()");
        exit(1);
    }

    
    //从网络上接受频道单包
    struct sockaddr_in serv_addr;
    socklen_t serv_addr_len = sizeof(serv_addr);
    int packet_len = 0;
    while (1) {
        packet_len = recvfrom(sd, msg_list, MSG_LIST_MAX, 0, (struct sockaddr*)&serv_addr, &serv_addr_len);
        if (packet_len < sizeof(struct msg_list_st)) {
            fprintf(stderr, "Ignore: Channel list packet too small.\n");
            continue;
        }
        if (msg_list->chnid != LIST_CHNID) {
            fprintf(stderr, "Ignore: Channel list id not match.\n");
            continue;
        }
        break;
    }

    //打印节目单，并选择频道
    struct msg_listentry_st *pos;
    for (pos = msg_list->entry; (char *)pos < ((char *)msg_list + packet_len);\
         pos = (struct msg_listentry_st*)((char*)pos + ntohs(pos->len))) {//这一块和老师的不一样，后期查看是否出问题
        printf("Channel [%d], %s\n", pos->chnid, pos->desc);//打印节目单
    }
    free(msg_list);//释放从网络接受数据的缓冲器

    int chosen = 0;//选择的频道id
    while (scanf("%d", &chosen) != 1) {
        fprintf(stderr, "请输入正确的频道号\n");
        exit(1);
    }
    

    //收节目单，收频道包发送给子进程
    struct msg_channel_st* msg_channel;
    msg_channel = malloc(MSG_CHANNEL_MAX); //申请一个频道包的最大长度
    if (msg_channel == NULL) {
        perror("malloc()");
        exit(1);
    }

    struct sockaddr_in rmot_addr;
    socklen_t rmot_addr_len = sizeof(rmot_addr);
    while (1) {
        packet_len = recvfrom(sd, msg_channel, MSG_CHANNEL_MAX, 0, (struct sockaddr*)&rmot_addr, &rmot_addr_len);
        if (rmot_addr.sin_addr.s_addr != serv_addr.sin_addr.s_addr || \
            rmot_addr.sin_port != serv_addr.sin_port) {
            fprintf(stderr, "Ignore: message address not match\n");
            continue;
        }
        if (packet_len < sizeof(struct msg_channel_st)) {
            fprintf(stderr, "Ignore: Channel packet too small.\n");
        }

        if (msg_channel->chnid == chosen) {
            fprintf(stdout, "Accept: channel[%d] recived\n", msg_channel->chnid);
            if (writen(pd[1], (char *)(msg_channel->data), packet_len - sizeof(chnid_t)) < 0)//写够packet_len - sizeof(chnid_t)个字节
                exit(1);//写入出错
        }

    }
    
    free(msg_channel);//可以结合信号处理和钩子函数处理
    close(sd);

    exit(0);
}