#include "head.h"


// 这是一个初始化进程池中进程的函数
// 第一个参数: 存储初始化进行的信息
// 第二个参数: 初始化几个进程
// 返回值: 0代表...-1....1...

void func(int num){
    printf("num: %d \n", num);
}
int initPool(son_status_t *list, int num){

    for(int i=0; i<num; i++){

        // 初始化本地socket: 用来和当前创建的子进程进行通信
        int socket_fd[2];
        socketpair(AF_LOCAL, SOCK_STREAM, 0, socket_fd);

        pid_t son_id = fork();
        if(son_id == 0){

            // signal(13, func);

            // 让子进程脱离前台进程组
            setpgid(0, 0);

            // 子进程逻辑
            close(socket_fd[1]);

            // 子进程的核心逻辑
            doWorker(socket_fd[0]);

        }else{
            // 父进程逻辑
            list[i].pid = son_id;
            list[i].flag = FREE;
            list[i].local_socket = socket_fd[1];

            close(socket_fd[0]);
        }

    }


    return 0;
}


// 要求在子进程中, 选取一个空闲进程, 处理客户端链接
// 第一个参数: 父进程记录子进程信息
// 第二个参数: 子进程的个数
// 第三个参数: 新的socket对象(新的客户端链接)
int toSonNetFd(son_status_t *list, int num, int net_fd){

    for(int i=0; i<num; i++){
        if(list[i].flag == FREE){
            // 使用本地socket传, net_fd以及其在文件描述符中的信息
            
            sendMsg(list[i].local_socket, net_fd, 0);

            list[i].flag = BUSY;
            break;
        }
    }

    return 0;
}
