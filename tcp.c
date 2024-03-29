// Copyright (c) 2012-2020 YAMAMOTO Masaya
// SPDX-License-Identifier: MIT

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "mmu.h"
#include "param.h"
#include "proc.h"
#include "common.h"
#include "net.h"
#include "ip.h"
#include "socket.h"



#define TCP_CB_TABLE_SIZE 16
#define TCP_SOURCE_PORT_MIN 49152
#define TCP_SOURCE_PORT_MAX 65535

#define TCP_CB_STATE_CLOSED      0
#define TCP_CB_STATE_LISTEN      1
#define TCP_CB_STATE_SYN_SENT    2
#define TCP_CB_STATE_SYN_RCVD    3
#define TCP_CB_STATE_ESTABLISHED 4
#define TCP_CB_STATE_FIN_WAIT1   5
#define TCP_CB_STATE_FIN_WAIT2   6
#define TCP_CB_STATE_CLOSING     7
#define TCP_CB_STATE_TIME_WAIT   8
#define TCP_CB_STATE_CLOSE_WAIT  9
#define TCP_CB_STATE_LAST_ACK    10

#define TCP_FLG_FIN 0x01
#define TCP_FLG_SYN 0x02
#define TCP_FLG_RST 0x04
#define TCP_FLG_PSH 0x08
#define TCP_FLG_ACK 0x10
#define TCP_FLG_URG 0x20

#define TCP_FLG_IS(x, y) ((x & 0x3f) == (y))
#define TCP_FLG_ISSET(x, y) ((x & 0x3f) & (y))

struct tcp_hdr {
    uint16_t src; // 源端口
    uint16_t dst; // 目的端口
    uint32_t seq; // 序列号，占用 32 位，用来标识 TCP 报文中的数据段的起始位置
    uint32_t ack; // 确认号，占用 32 位，用来确认对方已经接收到的数据段序号
    uint8_t  off; // 数据偏移，占用 8 位，表示 TCP 报文头部的长度，通常以 4 字节为单位，即乘以 4 后表示 TCP 报文头部的长度
    uint8_t  flg; // 控制标志位，占用 8 位，实际用6位。具体的标志位为：URG、ACK、PSH、RST、SYN、FIN
    uint16_t win; // 窗口大小，占用 16 位，表示发送端还能接收多少字节的数据
    uint16_t sum; // 校验和，占用 16 位，用来检测 TCP 报文在传输过程中是否出现错误
    uint16_t urg; // 紧急指针，占用 16 位，表示紧急数据的位置
};

struct tcp_txq_entry {
    struct tcp_hdr *segment;
    uint16_t len;
    //struct timeval timestamp;
    struct tcp_txq_entry *next;
};

struct tcp_txq_head {
    struct tcp_txq_entry *head;
    struct tcp_txq_entry *tail;
};

struct tcp_cb {
    uint8_t used;
    uint8_t state;
    struct netif *iface;
    uint16_t port;
    struct {
        ip_addr_t addr;
        uint16_t port;
    } peer;
    struct {
        uint32_t nxt; // 下一个要发送的序列号
        uint32_t una; // 未确认的序列号
        uint16_t up;
        uint32_t wl1;
        uint32_t wl2;
        uint16_t wnd;
    } snd;// 发送窗口相关信息，
    uint32_t iss; // 初始发送序列号
    struct {
        uint32_t nxt; // 下一个期望接收的序列号
        uint16_t up;
        uint16_t wnd;
    } rcv; // 接收窗口相关信息
    uint32_t irs; // 初始接收序列号
    struct tcp_txq_head txq;
    uint8_t window[4096];
    struct tcp_cb *parent;
    struct queue_head backlog;
};

#define TCP_CB_LISTENER_SIZE 128

#define TCP_CB_STATE_RX_ISREADY(x) (x->state == TCP_CB_STATE_ESTABLISHED || x->state == TCP_CB_STATE_FIN_WAIT1 || x->state == TCP_CB_STATE_FIN_WAIT2)
#define TCP_CB_STATE_TX_ISREADY(x) (x->state == TCP_CB_STATE_ESTABLISHED || x->state == TCP_CB_STATE_CLOSE_WAIT)

#define TCP_SOCKET_ISINVALID(x) (x < 0 || x >= TCP_CB_TABLE_SIZE)

//static pthread_t timer_thread;
static struct spinlock tcplock;
struct tcp_cb cb_table[TCP_CB_TABLE_SIZE];

static int
tcp_txq_add (struct tcp_cb *cb, struct tcp_hdr *hdr, size_t len) {
    struct tcp_txq_entry *txq;

    txq = (struct tcp_txq_entry *)kalloc();
    if (!txq) {
        return -1;
    }
    txq->segment = (struct tcp_hdr *)kalloc();
    if (!txq->segment) {
        kfree((char*)txq);
        return -1;
    }
    memcpy(txq->segment, hdr, len);
    txq->len = len;
    //gettimeofday(&txq->timestamp, NULL);
    txq->next = NULL;

    // set txq to next of tail entry
    if (cb->txq.head == NULL) {
        cb->txq.head = txq;
    } else {
        cb->txq.tail->next = txq;
    }
    // update tail entry
    cb->txq.tail = txq;

    return 0;
}

static int
tcp_cb_clear (struct tcp_cb *cb) {
    struct tcp_txq_entry *txq;
    struct queue_entry *entry;
    struct tcp_cb *backlog;

    while (cb->txq.head) {
        txq = cb->txq.head;
        cb->txq.head = txq->next;
        kfree((char*)txq->segment);
        kfree((char*)txq);
    }
    while (1) {
        entry = queue_pop(&cb->backlog);
        if (!entry) {
            break;
        }
        backlog = entry->data;
        kfree((char*)entry);
        tcp_cb_clear(backlog);
    }
    memset(cb, 0, sizeof(*cb));
    return 0;
}

static ssize_t
tcp_tx (struct tcp_cb *cb, uint32_t seq, uint32_t ack, uint8_t flg, uint8_t *buf, size_t len) {
    uint8_t segment[1500];
    struct tcp_hdr *hdr;
    ip_addr_t self, peer;
    uint32_t pseudo = 0;

    memset(&segment, 0, sizeof(segment));
    hdr = (struct tcp_hdr *)segment;
    hdr->src = cb->port;
    hdr->dst = cb->peer.port;
    hdr->seq = hton32(seq);
    hdr->ack = hton32(ack);
    hdr->off = (sizeof(struct tcp_hdr) >> 2) << 4;
    hdr->flg = flg;
    hdr->win = hton16(cb->rcv.wnd);
    hdr->sum = 0;
    hdr->urg = 0;
    memcpy(hdr + 1, buf, len);
    self = ((struct netif_ip *)cb->iface)->unicast;
    peer = cb->peer.addr;
    pseudo += (self >> 16) & 0xffff;
    pseudo += self & 0xffff;
    pseudo += (peer >> 16) & 0xffff;
    pseudo += peer & 0xffff;
    pseudo += hton16((uint16_t)IP_PROTOCOL_TCP);
    pseudo += hton16(sizeof(struct tcp_hdr) + len);
    hdr->sum = cksum16((uint16_t *)hdr, sizeof(struct tcp_hdr) + len, pseudo);
    ip_tx(cb->iface, IP_PROTOCOL_TCP, (uint8_t *)hdr, sizeof(struct tcp_hdr) + len, &peer);
    tcp_txq_add(cb, hdr, sizeof(struct tcp_hdr) + len);
    return len;
}

#if 0
static void *
tcp_timer_thread (void *arg) {
    struct timeval timestamp;
    struct tcp_cb *cb;
    struct tcp_txq_entry *txq, *prev, *tmp;
    ip_addr_t peer;

    while (1) {
        gettimeofday(&timestamp, NULL);
        acquire(&tcplock);
        for (cb = cb_table; cb < array_tailof(cb_table); cb++) {
            prev = NULL;
            txq = cb->txq.head;
            while (txq) {
                if (ntoh32(txq->segment->seq) >= cb->snd.una) {
                    if (timestamp.tv_sec - txq->timestamp.tv_sec > 3) {
                        peer = cb->peer.addr;
                        ip_tx(cb->iface, IP_PROTOCOL_TCP, (uint8_t *)txq->segment, txq->len, &peer);
                        txq->timestamp = timestamp;
                    }

                    // update previous tcp_txq_entry
                    prev = txq;
                    txq = txq->next;
                } else {
                    // remove tcp_txq_entry from list
                    // do not change prev, just update txq by txq->next,
                    // and free txq and txq->segment,
                    // and update cb->txq.[head|tail] if needed

                    // swap tail tcp_txq_entry
                    if (!txq->next) {
                        // txq is tail entry
                        cb->txq.tail = prev;
                    }
                    // swap previous tcp_txq_entry
                    if (prev) {
                        prev->next = txq->next;
                    } else {
                        cb->txq.head = txq->next;
                    }

                    // free tcp_txq_entry
                    tmp = txq->next;
                    free(txq->segment);
                    free(txq);
                    // check next entry
                    txq = tmp;
                }
            }
        }
        release(&tcplock);
        usleep(100000);
    }
    return NULL;
}
#endif

static void
tcp_incoming_event (struct tcp_cb *cb, struct tcp_hdr *hdr, size_t len) {
    uint32_t seq, ack;
    size_t hlen, plen;

    hlen = ((hdr->off >> 4) << 2);
    plen = len - hlen;
    switch (cb->state) {
        case TCP_CB_STATE_CLOSED:
            if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST)) {
                return;
            }
            if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK)) {
                seq = ntoh32(hdr->ack);
                ack = 0;
            } else {
                seq = 0;
                ack = ntoh32(hdr->seq);
                if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_SYN)) {
                    ack++;
                }
                if (plen) {
                    ack += plen;
                }
                if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_FIN)) {
                    ack++;
                }
            }
            tcp_tx(cb, seq, ack, TCP_FLG_RST, NULL, 0);
            return;
        case TCP_CB_STATE_LISTEN:
            if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST)) {
                return;
            }
            if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK)) {
                seq = ntoh32(hdr->ack);
                ack = 0;
                tcp_tx(cb, seq, ack, TCP_FLG_RST, NULL, 0);
                return;
            }
            if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_SYN)) {
                cb->rcv.nxt = ntoh32(hdr->seq) + 1;
                cb->irs = ntoh32(hdr->seq);
                cb->iss = (uint32_t)random();
                seq = cb->iss;
                ack = cb->rcv.nxt;
                tcp_tx(cb, seq, ack, TCP_FLG_SYN | TCP_FLG_ACK, NULL, 0);
                cb->snd.nxt = cb->iss + 1;
                cb->snd.una = cb->iss;
                cb->state = TCP_CB_STATE_SYN_RCVD;
            }
            return;
        case TCP_CB_STATE_SYN_SENT:
            if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK)) {
                if (ntoh32(hdr->ack) <= cb->iss || ntoh32(hdr->ack) > cb->snd.nxt) {
                    if (!TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST)) {
                        seq = ntoh32(hdr->ack);
                        ack = 0;
                        tcp_tx(cb, seq, ack, TCP_FLG_RST, NULL, 0);
                    }
                    return;
                }
            }
            if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST)) {
                if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK)) {
                    // TCB close
                }
                return;
            }
            if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_SYN)) {
                cb->rcv.nxt = ntoh32(hdr->seq) + 1;
                cb->irs = ntoh32(hdr->seq);
                if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK)) {
                    cb->snd.una = ntoh32(hdr->ack);
                    // delete TX queue
                    if (cb->snd.una > cb->iss) {
                        cb->state = TCP_CB_STATE_ESTABLISHED;
                        seq = cb->snd.nxt;
                        ack = cb->rcv.nxt;
                        tcp_tx(cb, seq, ack, TCP_FLG_ACK, NULL, 0);
                        wakeup(cb);
                    }
                    return;
                }
                seq = cb->iss;
                ack = cb->rcv.nxt;
                tcp_tx(cb, seq, ack, TCP_FLG_ACK, NULL, 0);
            }
            return;
        default:
            break;
    }
    if (ntoh32(hdr->seq) != cb->rcv.nxt) {
        // TODO
        return;
    }
    if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_RST | TCP_FLG_SYN)) {
        // TODO
        return;
    }
    if (!TCP_FLG_ISSET(hdr->flg, TCP_FLG_ACK)) {
        // TODO
        return;
    }
    switch (cb->state) {
        case TCP_CB_STATE_SYN_RCVD:
            if (cb->snd.una <= ntoh32(hdr->ack) && ntoh32(hdr->ack) <= cb->snd.nxt) {
                cb->state = TCP_CB_STATE_ESTABLISHED;
                queue_push(&cb->parent->backlog, cb, sizeof(*cb));
                wakeup(cb->parent);
            } else {
                tcp_tx(cb, ntoh32(hdr->ack), 0, TCP_FLG_RST, NULL, 0);
                break;
            }
        case TCP_CB_STATE_ESTABLISHED:
        case TCP_CB_STATE_FIN_WAIT1:
        case TCP_CB_STATE_FIN_WAIT2:
        case TCP_CB_STATE_CLOSE_WAIT:
        case TCP_CB_STATE_CLOSING:
            if (cb->snd.una < ntoh32(hdr->ack) && ntoh32(hdr->ack) <= cb->snd.nxt) {
                cb->snd.una = ntoh32(hdr->ack);
            } else if (ntoh32(hdr->ack) > cb->snd.nxt) {
                tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK, NULL, 0);
                return;
            }
            // send window update
            if (cb->state == TCP_CB_STATE_FIN_WAIT1) {
                if (ntoh32(hdr->ack) == cb->snd.nxt) {
                    cb->state = TCP_CB_STATE_FIN_WAIT2;
                }
            } else if (cb->state == TCP_CB_STATE_CLOSING) {
                if (ntoh32(hdr->ack) == cb->snd.nxt) {
                    cb->state = TCP_CB_STATE_TIME_WAIT;
                    wakeup(cb);
                }
                return;
            }
            break;
        case TCP_CB_STATE_LAST_ACK:
            wakeup(cb);
            tcp_cb_clear(cb); /* TCP_CB_STATE_CLOSED */
            return;
    }
    if (plen) {
        switch (cb->state) {
            case TCP_CB_STATE_ESTABLISHED:
            case TCP_CB_STATE_FIN_WAIT1:
            case TCP_CB_STATE_FIN_WAIT2:
                memcpy(cb->window + (sizeof(cb->window) - cb->rcv.wnd), (uint8_t *)hdr + hlen, plen);
                cb->rcv.nxt = ntoh32(hdr->seq) + plen;
                cb->rcv.wnd -= plen;
                seq = cb->snd.nxt;
                ack = cb->rcv.nxt;
                tcp_tx(cb, seq, ack, TCP_FLG_ACK, NULL, 0);
                wakeup(cb);
                break;
            default:
                break;
        }
    }
    if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_FIN)) {
        cb->rcv.nxt++;
        tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK, NULL, 0);
        switch (cb->state) {
            case TCP_CB_STATE_SYN_RCVD:
            case TCP_CB_STATE_ESTABLISHED:
                cb->state = TCP_CB_STATE_CLOSE_WAIT;
                wakeup(cb);
                break;
            case TCP_CB_STATE_FIN_WAIT1:
                cb->state = TCP_CB_STATE_FIN_WAIT2;
                break;
            case TCP_CB_STATE_FIN_WAIT2:
                cb->state = TCP_CB_STATE_TIME_WAIT;
                wakeup(cb);
                break;
            default:
                break;
        }
        return;
    }
    return;
}

static void
tcp_rx (uint8_t *segment, size_t len, ip_addr_t *src, ip_addr_t *dst, struct netif *iface) {
    struct tcp_hdr *hdr;
    uint32_t pseudo = 0;
    struct tcp_cb *cb, *fcb = NULL, *lcb = NULL; // fcb 监听状态的 TCP 控制块的指针，lcb 是指向监听状态的 TCP 控制块的指针

    if (*dst != ((struct netif_ip *)iface)->unicast) {
        return;
    }
    if (len < sizeof(struct tcp_hdr)) {
        return;
    }
    hdr = (struct tcp_hdr *)segment;
    pseudo += *src >> 16;
    pseudo += *src & 0xffff;
    pseudo += *dst >> 16;
    pseudo += *dst & 0xffff;
    pseudo += hton16((uint16_t)IP_PROTOCOL_TCP);
    pseudo += hton16(len);
    if (cksum16((uint16_t *)hdr, len, pseudo) != 0) {
        cprintf("tcp checksum error!\n");
        return;
    }
    acquire(&tcplock);
    for (cb = cb_table; cb < array_tailof(cb_table); cb++) {
        if (!cb->used) {
            if (!fcb) {
                fcb = cb;
            }
        }
        else if ((!cb->iface || cb->iface == iface) && cb->port == hdr->dst) {
            // 找到了与当前 TCP 报文匹配的 TCP 控制块
            if (cb->peer.addr == *src && cb->peer.port == hdr->src) {
                break;
            }
            if (cb->state == TCP_CB_STATE_LISTEN && !lcb) {
                lcb = cb;
            }
        }
    }
    if (cb == array_tailof(cb_table)) {
        if (!lcb || !fcb || !TCP_FLG_IS(hdr->flg, TCP_FLG_SYN)) {
            // send RST
            release(&tcplock);
            return;
        }
        cb = fcb;
        cb->used = 1;
        cb->state = lcb->state;
        cb->iface = iface;
        cb->port = lcb->port;
        cb->peer.addr = *src;
        cb->peer.port = hdr->src;
        cb->rcv.wnd = sizeof(cb->window);
        cb->parent = lcb;
    }
    tcp_incoming_event(cb, hdr, len);
    release(&tcplock);
    return;
}

int
tcp_api_open (void) {
    struct tcp_cb *cb;

    acquire(&tcplock);
    for (cb = cb_table; cb < array_tailof(cb_table); cb++) {
        if (!cb->used) {
            cb->used = 1;
            release(&tcplock);
            return array_offset(cb_table, cb);
        }
    }
    release(&tcplock);
    return -1;
}

int
tcp_api_close (int soc) {
    struct tcp_cb *cb;

    if (TCP_SOCKET_ISINVALID(soc)) {
        return -1;
    }
    acquire(&tcplock);
    cb = &cb_table[soc];
    if (!cb->used) {
        release(&tcplock);
        return -1;
    }
    switch (cb->state) {
        case TCP_CB_STATE_SYN_RCVD:
        case TCP_CB_STATE_ESTABLISHED:
            tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_FIN | TCP_FLG_ACK, NULL, 0);
            cb->state = TCP_CB_STATE_FIN_WAIT1;
            cb->snd.nxt++;
            sleep(cb, &tcplock);
            break;
        case TCP_CB_STATE_CLOSE_WAIT:
            tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_FIN | TCP_FLG_ACK, NULL, 0);
            cb->state = TCP_CB_STATE_LAST_ACK;
            cb->snd.nxt++;
            sleep(cb, &tcplock);
            break;
        default:
            break;
    }
    tcp_cb_clear(cb); /* TCP_CB_STATE_CLOSED */
    release(&tcplock);
    return 0;
}

int
tcp_api_connect (int soc, struct sockaddr *addr, int addrlen) {
    struct sockaddr_in *sin;
    struct tcp_cb *cb, *tmp;
    uint32_t p;

    if (TCP_SOCKET_ISINVALID(soc)) {
        return -1;
    }
    if (addr->sa_family != AF_INET) {
        return -1;
    }
    sin = (struct sockaddr_in *)addr;
    acquire(&tcplock);
    cb = &cb_table[soc];
    if (!cb->used || cb->state != TCP_CB_STATE_CLOSED) {
        release(&tcplock);
        return -1;
    }
    if (!cb->port) {
        int offset = time(NULL) % 1024;
        for (p = TCP_SOURCE_PORT_MIN + offset; p <= TCP_SOURCE_PORT_MAX; p++) {
            for (tmp = cb_table; tmp < array_tailof(cb_table); tmp++) {
                if (tmp->used && tmp->port == hton16((uint16_t)p)) {
                    break;
                }
            }
            if (tmp == array_tailof(cb_table)) {
                cb->port = hton16((uint16_t)p);
                break;
            }
        }
        if (!cb->port) {
            release(&tcplock);
            return -1;
        }
    }
    cb->peer.addr = sin->sin_addr;
    cb->peer.port = sin->sin_port;
    cb->rcv.wnd = sizeof(cb->window);
    cb->iss = (uint32_t)random(); //  Initial Sequence Number（初始序列号）是 TCP 协议中用于建立连接时的一个重要参数。TCP 连接的建立需要双方交换一些控制信息，其中包括序列号。iss 即是 TCP 发起连接时选择的初始序列号
    tcp_tx(cb, cb->iss, 0, TCP_FLG_SYN, NULL, 0);
    cb->snd.nxt = cb->iss + 1;
    cb->state = TCP_CB_STATE_SYN_SENT;
    while (cb->state == TCP_CB_STATE_SYN_SENT) {
        sleep(&cb_table[soc], &tcplock);
    }
    release(&tcplock);
    return 0;
}

int
tcp_api_bind (int soc, struct sockaddr *addr, int addrlen) {
    struct sockaddr_in *sin;
    struct tcp_cb *cb;

    if (TCP_SOCKET_ISINVALID(soc)) {
        return -1;
    }
    if (addr->sa_family != AF_INET) {
        return -1;
    }
    sin = (struct sockaddr_in *)addr;
    acquire(&tcplock);
    for (cb = cb_table; cb < array_tailof(cb_table); cb++) {
        if (cb->port == sin->sin_port) {
            release(&tcplock);
            return -1;
        }
    }
    cb = &cb_table[soc];
    if (!cb->used || cb->state != TCP_CB_STATE_CLOSED) {
        release(&tcplock);
        return -1;
    }
    cb->port = sin->sin_port;
    release(&tcplock);
    return 0;
}

int
tcp_api_listen (int soc, int backlog) {
    struct tcp_cb *cb;

    if (TCP_SOCKET_ISINVALID(soc)) {
        return -1;
    }
    acquire(&tcplock);
    cb = &cb_table[soc];
    if (!cb->used || cb->state != TCP_CB_STATE_CLOSED || !cb->port) {
        release(&tcplock);
        return -1;
    }
    cb->state = TCP_CB_STATE_LISTEN;
    release(&tcplock);
    return 0;
}

int
tcp_api_accept (int soc, struct sockaddr *addr, int *addrlen) {
    struct tcp_cb *cb, *backlog;
    struct queue_entry *entry;
    struct sockaddr_in *sin = NULL;

    if (TCP_SOCKET_ISINVALID(soc)) {
        return -1;
    }
    if (addr) {
        if (!addrlen) {
            return -1;
        }
        if (*addrlen < sizeof(struct sockaddr_in)) {
            return -1;
        }
        *addrlen = sizeof(struct sockaddr_in);
        sin = (struct sockaddr_in *)addr;
    }
    acquire(&tcplock);
    cb = &cb_table[soc];
    if (!cb->used) {
        release(&tcplock);
        return -1;
    }
    if (cb->state != TCP_CB_STATE_LISTEN) {
        release(&tcplock);
        return -1;
    }
    while ((entry = queue_pop(&cb->backlog)) == NULL) {
        if(myproc()->killed){
            release(&tcplock);
            return -1;
        }
        sleep(cb, &tcplock);
    }
    backlog = entry->data;
    kfree((char*)entry);
    if (sin) {
      sin->sin_family = AF_INET;
      sin->sin_addr = backlog->peer.addr;
      sin->sin_port = backlog->peer.port;
    }
    release(&tcplock);
    return array_offset(cb_table, backlog);
}

ssize_t
tcp_api_recv (int soc, uint8_t *buf, size_t size) {
    struct tcp_cb *cb;
    size_t total, len;

    if (TCP_SOCKET_ISINVALID(soc)) {
        return -1;
    }
    acquire(&tcplock);
    cb = &cb_table[soc];
    if (!cb->used) {
        release(&tcplock);
        return -1;
    }
    while (!(total = sizeof(cb->window) - cb->rcv.wnd)) {
        if (!TCP_CB_STATE_RX_ISREADY(cb)) {
            release(&tcplock);
            return 0;
        }
        if(myproc()->killed){
            release(&tcplock);
            return -1;
        }
        sleep(cb, &tcplock);
    }
    len = size < total ? size : total;
    memcpy(buf, cb->window, len);
    memmove(cb->window, cb->window + len, total - len);
    cb->rcv.wnd += len;
    release(&tcplock);
    return len;
}

ssize_t
tcp_api_send (int soc, uint8_t *buf, size_t len) {
    struct tcp_cb *cb;

    if (TCP_SOCKET_ISINVALID(soc)) {
        return -1;
    }
    acquire(&tcplock);
    cb = &cb_table[soc];
    if (!cb->used) {
        release(&tcplock);
        return -1;
    }
    if (!TCP_CB_STATE_TX_ISREADY(cb)) {
        release(&tcplock);
        return -1;
    }
    tcp_tx(cb, cb->snd.nxt, cb->rcv.nxt, TCP_FLG_ACK | TCP_FLG_PSH, buf, len);
    cb->snd.nxt += len;
    release(&tcplock);
    return 0;
}

int
tcp_init (void) {
    struct tcp_cb *cb;

    initlock(&tcplock, "tcplock");
    ip_add_protocol(IP_PROTOCOL_TCP, tcp_rx);
    return 0;
}
