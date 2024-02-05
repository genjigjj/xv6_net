xv6-net
=======
This project is implement TCP/IP Network Stack on xv6.

The network stack uses https://github.com/pandax381/microps

microps is a user-mode TCP/IP stack that I'm developing.
This project ported it to the xv6 kernel.

![demo](https://github.com/pandax381/xv6-net/blob/net/doc/demo.gif)

## Features

- [x] Network device
  - [x] PCI
    - [x] Bus scan
    - [x] Find device driver
  - [x] Intel 8254x (e1000) driver
    - [x] Initialization
    - [x] Basic operation of RX/TX with DMA
    - [x] Interrupt trap
    - [x] Detect interrupt souce (if multiple NICs)
  - [x] Device abstraction
    - [x] Define structure for physical device abstraction (struct netdev)
    - [x] Support multiple link protocols and physical devices
- [x] Protocols
  - [x] Ethernet
  - [x] ARP
  - [x] IP
  - [x] ICMP
  - [x] UDP
  - [x] TCP
- [x] Network Interface
  - [x] Interface abstraction
    - [x] Define structure for logical interface abstraction (struct netif)
    - [x] Support multiple address family and logical interfaces
  - [x] Configuration
    - [x] ifconfig
- [x] Socket API
  - [x] Systemcalls
    - [x] socket
    - [x] bind
    - [x] connect
    - [x] listen
    - [x] accept
    - [x] recv
    - [x] send
    - [x] recvfrom
    - [x] sendto
  - [x] Socket descriptor (compatible with File descriptor)
  - [x] Socket address (struct sockaddr)

## Task

- [ ] ARP resolution waiting queue (Currently discards data)
- [ ] TCP timer (Currently retransmission timer is not working)
- [ ] DHCP client
- [ ] DNS stub resolver

## Tutorial

*Build & Run*
```
$ sudo make docker-build
$ sudo make docker-run

...(xv6-net starts on qemu in the container)...

$ ifconfig net1 172.16.100.2 netmask 255.255.255.0
$ ifconfig net1 up
$ tcpechoserver
Starting TCP Echo Server
socket: success, soc=3
bind: success, self=0.0.0.0:7
waiting for connection...

...(client connection information and received data are output)...

(switch to qemu monitor with Ctrl-a + c and exit by typing `quit`)
```

*Ping Test (at another terminal)*
```
$ sudo docker exec -it xv6-net ping 172.16.100.2
```

*TCP Test (at another terminal)*
```
$ sudo docker exec -it xv6-net nc 172.16.100.2 7
```

## License

xv6: Under the MIT License. See [LICENSE](./LICENSE) file.

Additional code: Under the MIT License. See header of each source code.
