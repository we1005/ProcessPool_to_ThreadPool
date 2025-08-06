#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 任务结构体 */
typedef struct {
    void (*function)(void* arg);  /* 任务函数指针 */
    void* arg;                    /* 任务参数 */
} task_t;

/* 线程池结构体 */
typedef struct {
    pthread_t* threads;           /* 工作线程数组 */
    task_t* task_queue;          /* 任务队列 */
    int thread_count;            /* 线程数量 */
    int queue_size;              /* 队列大小 */
    int queue_front;             /* 队列头部索引 */
    int queue_rear;              /* 队列尾部索引 */
    int queue_count;             /* 当前队列中任务数量 */
    int shutdown;                /* 关闭标志 */
    pthread_mutex_t lock;        /* 互斥锁 */
    pthread_cond_t notify;       /* 条件变量 */
} threadpool_t;

/* 错误码定义 */
typedef enum {
    THREADPOOL_SUCCESS = 0,
    THREADPOOL_INVALID = -1,
    THREADPOOL_LOCK_FAILURE = -2,
    THREADPOOL_QUEUE_FULL = -3,
    THREADPOOL_SHUTDOWN = -4,
    THREADPOOL_THREAD_FAILURE = -5
} threadpool_error_t;

/**
 * 创建线程池
 * @param thread_count 线程数量
 * @param queue_size 任务队列大小
 * @return 线程池指针，失败返回NULL
 */
threadpool_t* threadpool_create(int thread_count, int queue_size);

/**
 * 向线程池添加任务
 * @param pool 线程池指针
 * @param function 任务函数
 * @param arg 任务参数
 * @return 成功返回0，失败返回错误码
 */
int threadpool_add(threadpool_t* pool, void (*function)(void*), void* arg);

/**
 * 销毁线程池
 * @param pool 线程池指针
 * @return 成功返回0，失败返回错误码
 */
int threadpool_destroy(threadpool_t* pool);

/**
 * 获取线程池中的线程数量
 * @param pool 线程池指针
 * @return 线程数量
 */
int threadpool_thread_count(threadpool_t* pool);

/**
 * 获取任务队列中的任务数量
 * @param pool 线程池指针
 * @return 任务数量
 */
int threadpool_queue_count(threadpool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* THREADPOOL_H */