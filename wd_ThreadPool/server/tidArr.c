#include "tidArr.h"
int tidArrInit(tidArr_t *tidArr, int workerNum){
    // 申请内存
    tidArr->arr = (pthread_t *)calloc(workerNum,sizeof(pthread_t));
    tidArr->workerNum = workerNum;
    return 0;
}
