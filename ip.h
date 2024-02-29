#define IP_HDR_SIZE_MIN 20
#define IP_HDR_SIZE_MAX 60
#define IP_PAYLOAD_SIZE_MAX (65535 - IP_HDR_SIZE_MIN)

#define IP_ADDR_LEN 4
#define IP_ADDR_STR_LEN 16 /* "ddd.ddd.ddd.ddd\0" */

#define IP_PROTOCOL_ICMP 0x01
#define IP_PROTOCOL_TCP  0x06
#define IP_PROTOCOL_UDP  0x11
#define IP_PROTOCOL_RAW  0xff

struct netif_ip {
    struct netif netif; // 网络接口
    ip_addr_t unicast; // 单播地址，即IP地址
    ip_addr_t netmask; // 子网掩码
    ip_addr_t network; // 网络接口所在网络的网络地址，与掩码运算后的地址
    ip_addr_t broadcast; // 广播地址
    ip_addr_t gateway; // 默认网关地址
};
