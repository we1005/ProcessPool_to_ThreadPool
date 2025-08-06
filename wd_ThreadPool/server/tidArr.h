#include <63func.h>
typedef struct tidArr_s {
    pthread_t * arr;//数组首地址
    int workerNum;//子线程数量  
} tidArr_t;
int tidArrInit(tidArr_t *tidArr, int workerNum);
