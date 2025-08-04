#include "head.h"


// 通过本地socket向对应的子进程发型一个net_fd相关的信息
int sendMsg(int local_socket, int net_fd, int flag){

    struct msghdr msg;
    bzero(&msg, sizeof(msg));

    // 准备正文信息
    struct iovec vec[1];
    vec[0].iov_base = &flag;
    vec[0].iov_len = sizeof(int);

    msg.msg_iov = vec;
    msg.msg_iovlen = 1;

    // 准备控制信息

    struct cmsghdr *cms = (struct cmsghdr *) malloc( CMSG_LEN(sizeof(int)) );
    cms->cmsg_len = CMSG_LEN(sizeof(int)); // 指明这个结构体的大小
    cms->cmsg_level = SOL_SOCKET;
    cms->cmsg_type = SCM_RIGHTS;

    void *p = CMSG_DATA(cms);
    int *fd = (int *)p;
    *fd = net_fd;

    msg.msg_control = cms;
    msg.msg_controllen = CMSG_LEN(sizeof(int));

    sendmsg(local_socket, &msg, 0);

    return 0;
}

// 子进程读取父进程通过本地socket发过来新的客户端的socket对象
int recvMsg(int local_socket, int *net_fd, int *flag){

    struct msghdr msg;
    bzero(&msg, sizeof(msg));

    // 准备正文信息
    int *num = (int *)malloc(sizeof(int));
    struct iovec vec[1];
    vec[0].iov_base = num;
    vec[0].iov_len = sizeof(int);

    msg.msg_iov = vec;
    msg.msg_iovlen = 1;

    // 准备控制信息
    struct cmsghdr *cms = (struct cmsghdr *) malloc( CMSG_LEN(sizeof(int)) );
    cms->cmsg_len = CMSG_LEN(sizeof(int)); // 指明这个结构体的大小
    cms->cmsg_level = SOL_SOCKET;
    cms->cmsg_type = SCM_RIGHTS;


    msg.msg_control = cms;
    msg.msg_controllen = CMSG_LEN(sizeof(int));

    recvmsg(local_socket, &msg, 0);


    void *p = CMSG_DATA(cms);
    int *fd = (int *)p;

    *net_fd = *fd;

    *flag = *num;

    free(num);
    return 0;
}
