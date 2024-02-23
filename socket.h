#include "sockio.h"

// 未指定的协议族
#define PF_UNSPEC   0
// 本地通信协议族
#define PF_LOCAL    1
// IPv4 网络协议族
#define PF_INET     2

#define AF_UNSPEC   PF_UNSPEC
#define AF_LOCAL    PF_LOCAL
// Address Family 的缩写，表示 IPv4 地址族（Address Family Internet Protocol）
#define AF_INET     PF_INET
// 表示流式套接字，通常用于面向连接的可靠数据传输，采用 TCP 协议。通过流式套接字传输的数据是可靠的、有序的，并且保证无差错地到达目的地。
#define SOCK_STREAM 1
// 表示数据报套接字，通常用于无连接的、不可靠数据传输，采用 UDP 协议。数据报套接字传输的数据是不可靠的、无序的，可能存在丢失或重复
#define SOCK_DGRAM  2

#define IPPROTO_TCP 0
#define IPPROTO_UDP 0

#define INADDR_ANY ((ip_addr_t)0)

struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    unsigned short sin_family;
    uint16_t sin_port;
    ip_addr_t sin_addr;
};

#define IFNAMSIZ 16

struct ifreq {
    char ifr_name[IFNAMSIZ]; /* Interface name */
    union {
        struct sockaddr ifr_addr;
        struct sockaddr ifr_dstaddr;
        struct sockaddr ifr_broadaddr;
        struct sockaddr ifr_netmask;
        struct sockaddr ifr_hwaddr;
        short           ifr_flags;
        int             ifr_ifindex;
        int             ifr_metric;
        int             ifr_mtu;
//      struct ifmap    ifr_map;
        char            ifr_slave[IFNAMSIZ];
        char            ifr_newname[IFNAMSIZ];
        char           *ifr_data;
    };
};
