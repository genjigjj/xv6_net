//
// Created by Transtech on 2024/2/29.
//
#include "types.h"
#include "user.h"
#include "socket.h"

int
main(int argc, char *argv[]) {
    if (argc != 3) {
        printf(1, "argc: failure\n");
        exit();
    }
    int soc = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    if (ip_addr_pton(argv[1], &addr.sin_addr) == -1) {
        printf(1, "ip_addr_pton: failure\n");
        close(soc);
        exit();
    }
    addr.sin_port = hton16(atoi(argv[2]));
    if (connect(soc, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        printf(1, "connect: failure\n");
        close(soc);
        exit();
    }
    char *buf = "hello world";
    send(soc, buf, strlen(buf));
    close(soc);
}