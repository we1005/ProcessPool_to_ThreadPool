#include "head.h"

// 定义管道:主进程自己给自己通信
int pipe_fd[2];

void func2(int num){
    //printf("main : num : %d \n", num);
    write(pipe_fd[1], "1", 1);
}
// 父进程的控制逻辑
int main(){

    // 启动子进程
    son_status_t list[4];
    initPool(list, 4);

    pipe(pipe_fd);
    signal(2, func2);

    // 初始化socket链接
    int socket_fd;
    initSocket(&socket_fd, "8080", "192.168.106.130");

    int epoll_fd = epoll_create(1);
    // 放入监听: 三次握手的socket,  和子进程进行通信的本地socket
    addEpoll(epoll_fd, socket_fd);
    for(int i=0; i<4; i++){
        addEpoll(epoll_fd, list[i].local_socket);
    }

    // 监听管道的读端
    addEpoll(epoll_fd, pipe_fd[0]);

    while(1){
        struct epoll_event events[10];
        memset(events, 0, sizeof(events));
        
        int epoll_num = epoll_wait(epoll_fd, events, 10, -1);

        for(int i=0; i<epoll_num; i++){
            int fd = events[i].data.fd;

            if(fd == pipe_fd[0]){
                //printf("管道可读, 意味着有人写管道,  信号处理函数在写管道, 说名有信号到来 \n");
                char buf[60] = {0};
                read(fd, buf, sizeof(buf));

                // 父进程收到信号, 说明要求退出, 先告诉子进程, 要求子进程退出
                // 怎么通知子进程退出, 通过和子进程链接的本地socket
                for(int j=0; j<4; j++){
                    sendMsg(list[j].local_socket, 1,  -1);
                }

                // 子进程退出完毕, 父进程再退出
                for(int j=0; j<4; j++){
                    wait(NULL);
                }

                // 走到这, 意味着子进程全部退出
                printf("子进程全部退出, 主进程也退出 \n");
                // TODO: close, free ....
                exit(0);

            }else if(fd == socket_fd){
                // 有新链接进来
                int net_fd = accept(socket_fd, NULL, NULL);
                // 把这个新的socket对象交给空闲的子进程处理
                toSonNetFd(list, 4, net_fd);

                close(net_fd);
                continue;
            }

            // 走到这:有子进程发信息
            for(int j=0; j<4; j++){
                if(list[j].local_socket == fd){
                    char buf[60] = {0};
                    recv(fd, buf, sizeof(buf), 0);
                    // 说明当前子进程, 要由忙状态改为闲状态
                    list[j].flag = FREE;
                    break;
                }
            }
        }
    }

    return 0;
}

