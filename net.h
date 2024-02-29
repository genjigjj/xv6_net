#define NETDEV_TYPE_ETHERNET  (0x0001)
#define NETDEV_TYPE_SLIP      (0x0002)

#include "if.h"

#define NETDEV_FLAG_BROADCAST IFF_BROADCAST
#define NETDEV_FLAG_MULTICAST IFF_MULTICAST
#define NETDEV_FLAG_P2P       IFF_POINTOPOINT
#define NETDEV_FLAG_LOOPBACK  IFF_LOOPBACK
#define NETDEV_FLAG_NOARP     IFF_NOARP
#define NETDEV_FLAG_PROMISC   IFF_PROMISC
#define NETDEV_FLAG_RUNNING   IFF_RUNNING
#define NETDEV_FLAG_UP        IFF_UP

#define NETPROTO_TYPE_IP      (0x0800)
#define NETPROTO_TYPE_ARP     (0x0806)
#define NETPROTO_TYPE_IPV6    (0x86dd)

#define NETIF_FAMILY_IPV4     (0x02)
#define NETIF_FAMILY_IPV6     (0x0a)

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

struct netdev;

struct netif {
    struct netif *next;
    uint8_t family; // 表示网络接口的类型，例如 IPv4 或 IPv6
    struct netdev *dev;
    /* Depends on implementation of protocols. */
};
// 网络设备操作
struct netdev_ops {
    int (*open)(struct netdev *dev); // 用于打开（初始化）网络设备
    int (*stop)(struct netdev *dev); // 用于停止网络设备
    int (*xmit)(struct netdev *dev, uint16_t type, const uint8_t *packet, size_t size, const void *dst); // 用于发送数据包到网络设备
};
// 网络设备
struct netdev {
    struct netdev *next; // 指向下一个网络设备结构体的指针，用于实现链表数据结构
    struct netif *ifs; // 指向网络接口结构体的指针，用于表示网络设备所属的网络接口
    int index;
    char name[IFNAMSIZ];
    uint16_t type;
    uint16_t mtu; // 最大传输单元（Maximum Transmission Unit），表示网络设备所支持的最大数据包大小
    uint16_t flags; // 标志位，用于表示网络设备的状态或属性
    uint16_t hlen; // 头部长度
    uint16_t alen; // 地址长度
    uint8_t addr[16]; // 网络设备的地址信息，即MAC（Media Access Control）地址
    uint8_t peer[16]; // 对等设备的地址信息
    uint8_t broadcast[16];
    struct netdev_ops *ops; // 指向网络设备操作函数集的指针，用于实现对网络设备的操作
    void *priv; // 指向私有数据的指针，用于存储网络设备相关的私有信息
};
