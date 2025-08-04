#include "head.h"


// 这是一个初始化socket服务端的函数
// 第一个参数: 传入传出, socket对象的文件描述符
// 第二个参数: 端口
// 第三个参数: ip
// 返回值...
int initSocket(int *socket_fd, char *port, char *ip){

    *socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    int reuse = 1;
    setsockopt(*socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in sockaddr;
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(atoi(port));
    sockaddr.sin_addr.s_addr = inet_addr(ip);
    bind(*socket_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));

    listen(*socket_fd, 10);
    return 0;
}
