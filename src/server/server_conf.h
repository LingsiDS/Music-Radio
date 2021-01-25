#ifndef SERVER_CONF_H__
#define SERVER_CONF_H__

#define DEFAULT_MEDIA_DIR "/home/lingsi/media"
#define DEFAULT_IF "eth0"

enum {
    RUN_DAEMON = 1,
    RUN_FRONT = 2
};

struct server_conf_st {
    char *rcvport;
    char *mgroup;
    char *media_dir;
    char runmod;
    char *ifname;
};
extern struct server_conf_st serv_conf;
extern int serv_sd;
extern struct sockaddr_in serv_addr;

#endif