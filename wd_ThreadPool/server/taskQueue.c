#include "taskQueue.h"
#include <63func.h>
int taskQueueInit(taskQueue_t *pqueue){
    bzero(pqueue,sizeof(taskQueue_t));
    return 0;
}
int enQueue(taskQueue_t *pqueue, int netfd){
    node_t * pNew = (node_t *)calloc(1,sizeof(node_t));
    pNew->netfd = netfd;
    if(pqueue->size == 0){
        pqueue->pFront = pNew;
        pqueue->pRear = pNew;
    }
    else{
        pqueue->pRear->pNext = pNew;
        pqueue->pRear = pNew;
    }
    ++pqueue->size;
    return 0;
}
int deQueue(taskQueue_t *pqueue){
    node_t *pCur = pqueue->pFront;
    pqueue->pFront = pCur->pNext;
    if(pqueue->pFront == NULL){
        pqueue->pRear = NULL;
    }
    free(pCur);
    --pqueue->size;
    return 0;
}

