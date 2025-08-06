#include "threadPool.h"
int tcpInit(const char *ip, const char *port){
    // socket --> bind --> listen
    int sockfd = socket(AF_INET,SOCK_STREAM,0);
    int flag = 1;
    setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(port));
    addr.sin_addr.s_addr = inet_addr(ip);
    bind(sockfd,(struct sockaddr *)&addr,sizeof(addr));
    listen(sockfd,10);
    return sockfd;
}
