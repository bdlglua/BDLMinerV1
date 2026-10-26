#pragma once
#include "Arduino.h"
#include <cerrno>
#include <sys/ioctl.h>
#include <sys/socket.h>
struct WiFiClient {
    int fd;
    explicit WiFiClient(int socket) : fd(socket) {}
    bool connected() {
        char c;int n=recv(fd,&c,1,MSG_PEEK|MSG_DONTWAIT);
        return n>0||(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK));
    }
    int available() { int bytes=0;ioctl(fd,FIONREAD,&bytes);return bytes; }
    int read(uint8_t *out,size_t size) { return recv(fd,out,std::min<size_t>(7,size),MSG_DONTWAIT); }
    size_t write(const uint8_t *in,size_t size) { int n=send(fd,in,std::min<size_t>(3,size),MSG_NOSIGNAL);return n>0?n:0; }
};
struct WiFiClass { bool isConnected() { return true; } };
extern WiFiClass WiFi;
