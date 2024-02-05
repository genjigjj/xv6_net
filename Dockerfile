FROM debian:10.2

RUN apt-get update && apt-get -y upgrade
RUN apt-get -y install build-essential gcc-multilib qemu-system-x86 netcat-openbsd

RUN mkdir -p /src/xv6-net
WORKDIR /src/xv6-net
COPY . .
