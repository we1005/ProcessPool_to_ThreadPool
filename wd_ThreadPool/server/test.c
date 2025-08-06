#include "taskQueue.h"
int main(int argc, char *argv[])
{
    taskQueue_t queue;
    taskQueueInit(&queue);
    enQueue(&queue,1);
    deQueue(&queue);
    return 0;
}

