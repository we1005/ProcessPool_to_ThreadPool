#include "head.h"


// 子进程的核心执行函数: 一直循环, 一直recvmsg读取任务
// 参数: 用来和父进程通信的本地socket的文件描述
int doWorker(int local_socket){

    while(1){


        // 读取任务
        int net_fd;
        int flag = 0;
        recvMsg(local_socket, &net_fd, &flag);
        if(flag == -1){
            // 说明父进程给我子进程发送了信息: 通过本地socket
            // 而且是要求退出的信息
            // TODO: close, free...

            printf("收到父进程的通知, 要求退出的通知 \n");
            exit(0);
        }

        // 执行任务: 服务客户端, 传文件
        toClientFile(net_fd);
        close(net_fd);
        
        // 通知父进程, 修改状态
        send(local_socket, "123", 3, 0);

    }

    return 0;
}

// 这个函数用来和客户端进行交互
int toClientFile(int net_fd){

    // TODO: 1, 接收客户端的文件名
    // TODO: 2, 判断文件是否存在

    // 回传文件给客户端
    sendFile(net_fd);


    return 0;
}
