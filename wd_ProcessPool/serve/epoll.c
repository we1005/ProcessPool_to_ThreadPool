#include "head.h"


// 把要监听的文件描述符放入epoll监听
// 第一个参数: epoll对象的文件描述符
// 第二个参数: 要放入监听的文件描述符
// 返回值
int addEpoll(int epoll_fd, int fd){

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);

    return 0;
}
