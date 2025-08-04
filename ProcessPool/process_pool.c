#include "process_pool.h"

/* 全局变量，用于信号处理 */
static volatile sig_atomic_t pool_should_exit = 0;
static process_pool_t* global_pool = NULL;

/* 外部函数声明 */
extern int create_worker_process(worker_process_t* worker, task_handler_t handler);
extern void terminate_worker_process(worker_process_t* worker);
extern int create_pipe_pair(int pipefd[2]);
extern void close_pipe_pair(int pipefd[2]);
extern int send_task_to_worker(worker_process_t* worker, const task_t* task);
extern int receive_result_from_worker(worker_process_t* worker, task_result_t* result);
extern int is_worker_alive(worker_process_t* worker);
extern void reap_dead_children(process_pool_t* pool);

/**
 * 主进程信号处理函数
 */
static void master_signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            pool_should_exit = 1;
            break;
        case SIGCHLD:
            /* 子进程退出信号，在主循环中处理 */
            break;
        case SIGPIPE:
            /* 忽略SIGPIPE */
            break;
        default:
            break;
    }
}

/**
 * 设置主进程信号处理
 */
static int setup_master_signals(void) {
    struct sigaction sa;
    
    sa.sa_handler = master_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        log_message("ERROR", "sigaction SIGTERM failed: %s", strerror(errno));
        return -1;
    }
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        log_message("ERROR", "sigaction SIGINT failed: %s", strerror(errno));
        return -1;
    }
    
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        log_message("ERROR", "sigaction SIGCHLD failed: %s", strerror(errno));
        return -1;
    }
    
    /* 忽略SIGPIPE */
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        log_message("ERROR", "sigaction SIGPIPE failed: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

/**
 * 初始化任务队列
 */
static void init_task_queue(task_queue_t* queue) {
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

/**
 * 向任务队列添加任务
 */
static int enqueue_task(task_queue_t* queue, const task_t* task) {
    if (queue->count >= MAX_TASK_QUEUE) {
        return -1;  /* 队列已满 */
    }
    
    queue->tasks[queue->tail] = *task;
    queue->tail = (queue->tail + 1) % MAX_TASK_QUEUE;
    queue->count++;
    
    return 0;
}

/**
 * 从任务队列取出任务
 */
static int dequeue_task(task_queue_t* queue, task_t* task) {
    if (queue->count == 0) {
        return -1;  /* 队列为空 */
    }
    
    *task = queue->tasks[queue->head];
    queue->head = (queue->head + 1) % MAX_TASK_QUEUE;
    queue->count--;
    
    return 0;
}

/**
 * 查找空闲的工作进程
 */
static worker_process_t* find_idle_worker(process_pool_t* pool) {
    for (int i = 0; i < pool->worker_count; i++) {
        if (pool->workers[i].status == WORKER_IDLE && 
            is_worker_alive(&pool->workers[i])) {
            return &pool->workers[i];
        }
    }
    return NULL;
}

/**
 * 重启死亡的工作进程
 */
static int restart_dead_worker(process_pool_t* pool, int worker_index, task_handler_t handler) {
    worker_process_t* worker = &pool->workers[worker_index];
    
    if (worker->status != WORKER_DEAD) {
        return 0;  /* 进程还活着 */
    }
    
    log_message("INFO", "Restarting dead worker at index %d", worker_index);
    
    /* 清理旧的资源 */
    close_pipe_pair(worker->pipe_to_worker);
    close_pipe_pair(worker->pipe_from_worker);
    
    /* 创建新的工作进程 */
    if (create_worker_process(worker, handler) == -1) {
        log_message("ERROR", "Failed to restart worker at index %d", worker_index);
        return -1;
    }
    
    return 0;
}

/**
 * 更新文件描述符集合
 */
static void update_fd_sets(process_pool_t* pool) {
    FD_ZERO(&pool->read_fds);
    pool->max_fd = -1;
    
    for (int i = 0; i < pool->worker_count; i++) {
        worker_process_t* worker = &pool->workers[i];
        if (worker->status != WORKER_DEAD && worker->pipe_from_worker[0] != -1) {
            FD_SET(worker->pipe_from_worker[0], &pool->read_fds);
            if (worker->pipe_from_worker[0] > pool->max_fd) {
                pool->max_fd = worker->pipe_from_worker[0];
            }
        }
    }
}

/**
 * 分发任务给工作进程
 */
static int dispatch_tasks(process_pool_t* pool) {
    task_t task;
    worker_process_t* worker;
    int dispatched = 0;
    
    while (pool->task_queue.count > 0) {
        worker = find_idle_worker(pool);
        if (worker == NULL) {
            break;  /* 没有空闲的工作进程 */
        }
        
        if (dequeue_task(&pool->task_queue, &task) == -1) {
            break;  /* 队列为空 */
        }
        
        if (send_task_to_worker(worker, &task) == -1) {
            /* 发送失败，将任务重新放回队列 */
            enqueue_task(&pool->task_queue, &task);
            worker->status = WORKER_DEAD;
            break;
        }
        
        dispatched++;
    }
    
    return dispatched;
}

/**
 * 创建进程池
 */
process_pool_t* process_pool_create(int worker_count, task_handler_t handler) {
    if (worker_count <= 0 || worker_count > MAX_WORKERS) {
        log_message("ERROR", "Invalid worker count: %d", worker_count);
        return NULL;
    }
    
    process_pool_t* pool = malloc(sizeof(process_pool_t));
    if (pool == NULL) {
        log_message("ERROR", "Failed to allocate memory for process pool");
        return NULL;
    }
    
    memset(pool, 0, sizeof(process_pool_t));
    pool->worker_count = worker_count;
    pool->running = 0;
    pool->next_task_id = 1;
    
    /* 初始化任务队列 */
    init_task_queue(&pool->task_queue);
    
    /* 设置信号处理 */
    if (setup_master_signals() == -1) {
        free(pool);
        return NULL;
    }
    
    /* 创建工作进程 */
    for (int i = 0; i < worker_count; i++) {
        if (create_worker_process(&pool->workers[i], handler) == -1) {
            log_message("ERROR", "Failed to create worker %d", i);
            /* 清理已创建的工作进程 */
            for (int j = 0; j < i; j++) {
                terminate_worker_process(&pool->workers[j]);
            }
            free(pool);
            return NULL;
        }
    }
    
    global_pool = pool;
    log_message("INFO", "Process pool created with %d workers", worker_count);
    
    return pool;
}

/**
 * 提交任务到进程池
 */
int process_pool_submit_task(process_pool_t* pool, const char* task_data, int data_len) {
    if (pool == NULL || task_data == NULL || data_len <= 0 || data_len >= MAX_TASK_DATA) {
        return -1;
    }
    
    if (pool->task_queue.count >= MAX_TASK_QUEUE) {
        log_message("WARN", "Task queue is full");
        return -1;
    }
    
    task_t task;
    task.task_id = pool->next_task_id++;
    task.data_len = data_len;
    memcpy(task.data, task_data, data_len);
    task.data[data_len] = '\0';
    
    if (enqueue_task(&pool->task_queue, &task) == -1) {
        log_message("ERROR", "Failed to enqueue task %d", task.task_id);
        return -1;
    }
    
    log_message("DEBUG", "Task %d submitted to pool", task.task_id);
    return task.task_id;
}

/**
 * 获取任务结果
 */
int process_pool_get_result(process_pool_t* pool, task_result_t* result, int timeout_ms) {
    if (pool == NULL || result == NULL) {
        return -1;
    }
    
    fd_set read_fds;
    struct timeval timeout;
    struct timeval* timeout_ptr = NULL;
    
    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        timeout_ptr = &timeout;
    }
    
    while (pool->running) {
        update_fd_sets(pool);
        
        if (pool->max_fd == -1) {
            /* 没有活跃的工作进程 */
            usleep(10000);  /* 10ms */
            continue;
        }
        
        read_fds = pool->read_fds;
        int select_result = select(pool->max_fd + 1, &read_fds, NULL, NULL, timeout_ptr);
        
        if (select_result == -1) {
            if (errno == EINTR) {
                continue;
            }
            log_message("ERROR", "select failed: %s", strerror(errno));
            return -1;
        }
        
        if (select_result == 0) {
            /* 超时 */
            return -1;
        }
        
        /* 检查哪个工作进程有结果可读 */
        for (int i = 0; i < pool->worker_count; i++) {
            worker_process_t* worker = &pool->workers[i];
            if (worker->status != WORKER_DEAD && 
                worker->pipe_from_worker[0] != -1 &&
                FD_ISSET(worker->pipe_from_worker[0], &read_fds)) {
                
                if (receive_result_from_worker(worker, result) == 0) {
                    return 0;  /* 成功获取结果 */
                }
            }
        }
    }
    
    return -1;
}

/**
 * 运行进程池主循环
 */
int process_pool_run(process_pool_t* pool) {
    if (pool == NULL) {
        return -1;
    }
    
    pool->running = 1;
    log_message("INFO", "Process pool started");
    
    while (pool->running && !pool_should_exit) {
        /* 回收死亡的子进程 */
        reap_dead_children(pool);
        
        /* 重启死亡的工作进程 */
        for (int i = 0; i < pool->worker_count; i++) {
            if (pool->workers[i].status == WORKER_DEAD) {
                restart_dead_worker(pool, i, NULL);  /* 使用默认处理函数 */
            }
        }
        
        /* 分发任务 */
        dispatch_tasks(pool);
        
        /* 短暂休眠，避免CPU占用过高 */
        usleep(10000);  /* 10ms */
    }
    
    pool->running = 0;
    log_message("INFO", "Process pool stopped");
    
    return 0;
}

/**
 * 停止进程池
 */
void process_pool_stop(process_pool_t* pool) {
    if (pool != NULL) {
        pool->running = 0;
        log_message("INFO", "Process pool stop requested");
    }
}

/**
 * 销毁进程池
 */
void process_pool_destroy(process_pool_t* pool) {
    if (pool == NULL) {
        return;
    }
    
    log_message("INFO", "Destroying process pool");
    
    /* 停止运行 */
    pool->running = 0;
    
    /* 终止所有工作进程 */
    for (int i = 0; i < pool->worker_count; i++) {
        terminate_worker_process(&pool->workers[i]);
    }
    
    /* 回收剩余的子进程 */
    reap_dead_children(pool);
    
    /* 释放内存 */
    free(pool);
    
    if (global_pool == pool) {
        global_pool = NULL;
    }
    
    log_message("INFO", "Process pool destroyed");
}

/**
 * 获取进程池状态信息
 */
void process_pool_get_status(process_pool_t* pool, int* active_workers, int* pending_tasks) {
    if (pool == NULL) {
        if (active_workers) *active_workers = 0;
        if (pending_tasks) *pending_tasks = 0;
        return;
    }
    
    int active = 0;
    for (int i = 0; i < pool->worker_count; i++) {
        if (pool->workers[i].status != WORKER_DEAD) {
            active++;
        }
    }
    
    if (active_workers) *active_workers = active;
    if (pending_tasks) *pending_tasks = pool->task_queue.count;
}