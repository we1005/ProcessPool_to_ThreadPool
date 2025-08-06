#include "threadPool.h"
int threadPoolInit(threadPool_t *pthreadPool, int workerNum){
    tidArrInit(&pthreadPool->tidArr,workerNum);
    taskQueueInit(&pthreadPool->taskQueue);
    pthread_mutex_init(&pthreadPool->mutex,NULL);
    pthread_cond_init(&pthreadPool->cond,NULL);
    pthreadPool->exitFlag = 0; // 一开始不用退出
    return 0;
}
int makeWorker(threadPool_t *pthreadPool){
    for(int i = 0; i < pthreadPool->tidArr.workerNum; ++i){
        pthread_create(&pthreadPool->tidArr.arr[i],NULL,threadFunc,pthreadPool);
    }
    return 0;
}
// 简单退出
//void unlock(void *arg){
//    threadPool_t * pthreadPool = (threadPool_t *)arg;
//    printf("unlock\n");
//    pthread_mutex_unlock(&pthreadPool->mutex);
//}
//void *threadFunc(void *arg){
//    threadPool_t *pthreadPool = (threadPool_t *)arg;
//    printf("I am worker!\n");
//    while(1){
//        int netfd;
//        pthread_mutex_lock(&pthreadPool->mutex);
//        pthread_cleanup_push(unlock,pthreadPool);
//        while(pthreadPool->taskQueue.size == 0){
//            pthread_cond_wait(&pthreadPool->cond,&pthreadPool->mutex);
//        }
//        netfd = pthreadPool->taskQueue.pFront->netfd;
//        deQueue(&pthreadPool->taskQueue);
//        //pthread_mutex_unlock(&pthreadPool->mutex);
//        pthread_cleanup_pop(1);
//        transFile(netfd); //业务是下载文件
//        close(netfd);
//    }
//    pthread_exit(NULL);
//}
// 优雅退出
void *threadFunc(void *arg){
    threadPool_t *pthreadPool = (threadPool_t *)arg;
    printf("I am worker!\n");
    while(1){
        int netfd;
        pthread_mutex_lock(&pthreadPool->mutex);
        while(pthreadPool->exitFlag == 0 && pthreadPool->taskQueue.size == 0){
            pthread_cond_wait(&pthreadPool->cond,&pthreadPool->mutex);
        }
        //要么是线程池终止
        if(pthreadPool->exitFlag == 1){
            printf("I am worker. I am going to exit!\n");
            pthread_mutex_unlock(&pthreadPool->mutex);
            pthread_exit(NULL);
        }
        //要么是有任务到来
        netfd = pthreadPool->taskQueue.pFront->netfd;
        deQueue(&pthreadPool->taskQueue);
        pthread_mutex_unlock(&pthreadPool->mutex);
        transFile(netfd); //业务是下载文件
        close(netfd);
    }
    pthread_exit(NULL);
}
