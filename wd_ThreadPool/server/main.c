#include <63func.h>
#include "threadPool.h"
int exitPipe[2];
void handler(int signum){
    printf("signum = %d\n", signum);
    write(exitPipe[1],"1",1);
}
int main(int argc, char *argv[])
{
    // ./server 192.168.72.128 12345 3
    ARGS_CHECK(argc,4);

    pipe(exitPipe);// exitPipe[0] --> 读端  exitPipe[1] --> 写端
    if(fork()){
        close(exitPipe[0]);
        signal(SIGUSR1,handler);
        wait(NULL);//等待子进程的终止
        printf("Parent is going to exit!\n");
        exit(0);
    }
    // 只有子进程能走这里
    close(exitPipe[1]);
    
    threadPool_t threadPool;
    threadPoolInit(&threadPool, atoi(argv[3]));
    makeWorker(&threadPool);

    int sockfd = tcpInit(argv[1],argv[2]);
    int epfd = epoll_create(1);
    epollAdd(epfd,sockfd); 
    epollAdd(epfd,exitPipe[0]); //监听管道的读端

    struct epoll_event readyset[1024];
    while(1){
        int readynum = epoll_wait(epfd,readyset,1024,-1);
        for(int i = 0; i < readynum; ++i){
            if(readyset[i].data.fd == sockfd){
                int netfd = accept(sockfd,NULL,NULL);
                printf("netfd = %d\n", netfd);
                pthread_mutex_lock(&threadPool.mutex);
                enQueue(&threadPool.taskQueue,netfd);
                pthread_cond_broadcast(&threadPool.cond);
                pthread_mutex_unlock(&threadPool.mutex);
            }
            else if(readyset[i].data.fd == exitPipe[0]){
                printf("Child is going to exit!\n");
                // 方案1 无论工人线程是否在干活，强制退
                //for(int i = 0; i < threadPool.tidArr.workerNum; ++i){
                //    pthread_cancel(threadPool.tidArr.arr[i]);
                //}
                //for(int i = 0; i < threadPool.tidArr.workerNum; ++i){
                //    pthread_join(threadPool.tidArr.arr[i],NULL);
                //}
                //printf("Child has exited!\n");
                //exit(0);
                // 方案2 等到工人线程干完活以后再退出
                pthread_mutex_lock(&threadPool.mutex);
                threadPool.exitFlag = 1; // 改变退出标志位
                pthread_cond_broadcast(&threadPool.cond); //通知所有正在等待的子线程
                pthread_mutex_unlock(&threadPool.mutex);
                for(int i = 0; i < threadPool.tidArr.workerNum; ++i){
                    pthread_join(threadPool.tidArr.arr[i],NULL);
                }
                printf("Child has exited!\n");
                exit(0);
            }
        }
        
    }
    return 0;
}

