#include "threadpool.h"

/* 工作线程函数 */
static void* threadpool_thread(void* threadpool);

/* 释放线程池资源 */
static int threadpool_free(threadpool_t* pool);

threadpool_t* threadpool_create(int thread_count, int queue_size) {
    threadpool_t* pool;
    int i;
    
    /* 参数检查 */
    if (thread_count <= 0 || thread_count > 1000 || queue_size <= 0 || queue_size > 65536) {
        return NULL;
    }
    
    /* 分配线程池内存 */
    pool = (threadpool_t*)malloc(sizeof(threadpool_t));
    if (pool == NULL) {
        return NULL;
    }
    
    /* 初始化线程池 */
    pool->thread_count = 0;
    pool->queue_size = queue_size;
    pool->queue_front = 0;
    pool->queue_rear = 0;
    pool->queue_count = 0;
    pool->shutdown = 0;
    
    /* 分配线程数组 */
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * thread_count);
    if (pool->threads == NULL) {
        free(pool);
        return NULL;
    }
    
    /* 分配任务队列 */
    pool->task_queue = (task_t*)malloc(sizeof(task_t) * queue_size);
    if (pool->task_queue == NULL) {
        free(pool->threads);
        free(pool);
        return NULL;
    }
    
    /* 初始化互斥锁和条件变量 */
    if (pthread_mutex_init(&(pool->lock), NULL) != 0 ||
        pthread_cond_init(&(pool->notify), NULL) != 0) {
        free(pool->task_queue);
        free(pool->threads);
        free(pool);
        return NULL;
    }
    
    /* 创建工作线程 */
    for (i = 0; i < thread_count; i++) {
        if (pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void*)pool) != 0) {
            threadpool_destroy(pool);
            return NULL;
        }
        pool->thread_count++;
    }
    
    return pool;
}

int threadpool_add(threadpool_t* pool, void (*function)(void*), void* arg) {
    int err = THREADPOOL_SUCCESS;
    int next;
    
    if (pool == NULL || function == NULL) {
        return THREADPOOL_INVALID;
    }
    
    if (pthread_mutex_lock(&(pool->lock)) != 0) {
        return THREADPOOL_LOCK_FAILURE;
    }
    
    do {
        /* 检查线程池是否已关闭 */
        if (pool->shutdown) {
            err = THREADPOOL_SHUTDOWN;
            break;
        }
        
        /* 检查队列是否已满 */
        if (pool->queue_count == pool->queue_size) {
            err = THREADPOOL_QUEUE_FULL;
            break;
        }
        
        /* 添加任务到队列 */
        next = (pool->queue_rear + 1) % pool->queue_size;
        pool->task_queue[pool->queue_rear].function = function;
        pool->task_queue[pool->queue_rear].arg = arg;
        pool->queue_rear = next;
        pool->queue_count++;
        
        /* 通知工作线程 */
        if (pthread_cond_signal(&(pool->notify)) != 0) {
            err = THREADPOOL_LOCK_FAILURE;
            break;
        }
    } while (0);
    
    if (pthread_mutex_unlock(&pool->lock) != 0) {
        err = THREADPOOL_LOCK_FAILURE;
    }
    
    return err;
}

int threadpool_destroy(threadpool_t* pool) {
    int i, err = THREADPOOL_SUCCESS;
    
    if (pool == NULL) {
        return THREADPOOL_INVALID;
    }
    
    if (pthread_mutex_lock(&(pool->lock)) != 0) {
        return THREADPOOL_LOCK_FAILURE;
    }
    
    do {
        /* 检查是否已经在关闭过程中 */
        if (pool->shutdown) {
            err = THREADPOOL_SHUTDOWN;
            break;
        }
        
        pool->shutdown = 1;
        
        /* 唤醒所有工作线程 */
        if (pthread_cond_broadcast(&(pool->notify)) != 0 ||
            pthread_mutex_unlock(&(pool->lock)) != 0) {
            err = THREADPOOL_LOCK_FAILURE;
            break;
        }
        
        /* 等待所有工作线程结束 */
        for (i = 0; i < pool->thread_count; i++) {
            if (pthread_join(pool->threads[i], NULL) != 0) {
                err = THREADPOOL_THREAD_FAILURE;
            }
        }
    } while (0);
    
    /* 释放资源 */
    if (!err) {
        threadpool_free(pool);
    }
    
    return err;
}

int threadpool_thread_count(threadpool_t* pool) {
    if (pool == NULL) {
        return -1;
    }
    return pool->thread_count;
}

int threadpool_queue_count(threadpool_t* pool) {
    int count;
    if (pool == NULL) {
        return -1;
    }
    
    pthread_mutex_lock(&(pool->lock));
    count = pool->queue_count;
    pthread_mutex_unlock(&(pool->lock));
    
    return count;
}

static void* threadpool_thread(void* threadpool) {
    threadpool_t* pool = (threadpool_t*)threadpool;
    task_t task;
    
    for (;;) {
        /* 加锁 */
        pthread_mutex_lock(&(pool->lock));
        
        /* 等待任务或关闭信号 */
        while ((pool->queue_count == 0) && (!pool->shutdown)) {
            pthread_cond_wait(&(pool->notify), &(pool->lock));
        }
        
        /* 检查是否需要关闭 */
        if (pool->shutdown) {
            break;
        }
        
        /* 从队列中取出任务 */
        task.function = pool->task_queue[pool->queue_front].function;
        task.arg = pool->task_queue[pool->queue_front].arg;
        pool->queue_front = (pool->queue_front + 1) % pool->queue_size;
        pool->queue_count--;
        
        /* 解锁 */
        pthread_mutex_unlock(&(pool->lock));
        
        /* 执行任务 */
        (*(task.function))(task.arg);
    }
    
    pool->thread_count--;
    pthread_mutex_unlock(&(pool->lock));
    pthread_exit(NULL);
    return NULL;
}

static int threadpool_free(threadpool_t* pool) {
    if (pool == NULL || pool->thread_count > 0) {
        return THREADPOOL_INVALID;
    }
    
    /* 释放资源 */
    if (pool->threads) {
        free(pool->threads);
        free(pool->task_queue);
        
        /* 销毁互斥锁和条件变量 */
        pthread_mutex_lock(&(pool->lock));
        pthread_mutex_destroy(&(pool->lock));
        pthread_cond_destroy(&(pool->notify));
    }
    
    free(pool);
    return THREADPOOL_SUCCESS;
}