#include <63func.h>
#include "taskQueue.h"
#include "tidArr.h"
typedef struct threadPool_s {
    // 多个线程的pthread_t
    tidArr_t tidArr;
    // 任务队列
    taskQueue_t taskQueue;
    // 锁
    pthread_mutex_t mutex;
    // 条件变量
    pthread_cond_t cond;
    // 退出标志
    int exitFlag;
} threadPool_t;

int threadPoolInit(threadPool_t *pthreadPool, int workerNum);
int makeWorker(threadPool_t *pthreadPool);
void *threadFunc(void *arg);
int tcpInit(const char *ip, const char *port); 
int epollAdd(int epfd, int fd);
int epollDel(int epfd, int fd);
int transFile(int netfd);
