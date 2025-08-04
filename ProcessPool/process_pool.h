#ifndef PROCESS_POOL_H
#define PROCESS_POOL_H

#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

/* 最大工作进程数 */
#define MAX_WORKERS 32
/* 任务队列最大长度 */
#define MAX_TASK_QUEUE 1024
/* 管道缓冲区大小 */
#define PIPE_BUF_SIZE 4096
/* 任务数据最大长度 */
#define MAX_TASK_DATA 1024

/* 工作进程状态 */
typedef enum {
    WORKER_IDLE = 0,    /* 空闲 */
    WORKER_BUSY,        /* 忙碌 */
    WORKER_DEAD         /* 死亡 */
} worker_status_t;

/* 任务类型 */
typedef struct {
    int task_id;                        /* 任务ID */
    char data[MAX_TASK_DATA];          /* 任务数据 */
    int data_len;                      /* 数据长度 */
} task_t;

/* 任务结果 */
typedef struct {
    int task_id;                       /* 任务ID */
    int result_code;                   /* 结果码 */
    char result_data[MAX_TASK_DATA];   /* 结果数据 */
    int result_len;                    /* 结果长度 */
} task_result_t;

/* 工作进程信息 */
typedef struct {
    pid_t pid;                         /* 进程ID */
    worker_status_t status;            /* 进程状态 */
    int pipe_to_worker[2];             /* 主进程到工作进程的管道 */
    int pipe_from_worker[2];           /* 工作进程到主进程的管道 */
    time_t last_active;                /* 最后活跃时间 */
    int current_task_id;               /* 当前处理的任务ID */
} worker_process_t;

/* 任务队列 */
typedef struct {
    task_t tasks[MAX_TASK_QUEUE];      /* 任务数组 */
    int head;                          /* 队列头 */
    int tail;                          /* 队列尾 */
    int count;                         /* 任务数量 */
} task_queue_t;

/* 进程池结构 */
typedef struct {
    worker_process_t workers[MAX_WORKERS];  /* 工作进程数组 */
    int worker_count;                       /* 工作进程数量 */
    task_queue_t task_queue;                /* 任务队列 */
    int running;                            /* 运行状态 */
    int next_task_id;                       /* 下一个任务ID */
    fd_set read_fds;                        /* 读文件描述符集合 */
    int max_fd;                             /* 最大文件描述符 */
} process_pool_t;

/* 任务处理函数类型 */
typedef int (*task_handler_t)(const char* task_data, int data_len, char* result_data, int* result_len);

/* 进程池接口函数 */

/**
 * 创建进程池
 * @param worker_count 工作进程数量
 * @param handler 任务处理函数
 * @return 进程池指针，失败返回NULL
 */
process_pool_t* process_pool_create(int worker_count, task_handler_t handler);

/**
 * 提交任务到进程池
 * @param pool 进程池指针
 * @param task_data 任务数据
 * @param data_len 数据长度
 * @return 任务ID，失败返回-1
 */
int process_pool_submit_task(process_pool_t* pool, const char* task_data, int data_len);

/**
 * 获取任务结果
 * @param pool 进程池指针
 * @param result 结果结构体指针
 * @param timeout_ms 超时时间(毫秒)，-1表示阻塞等待
 * @return 0成功，-1失败
 */
int process_pool_get_result(process_pool_t* pool, task_result_t* result, int timeout_ms);

/**
 * 运行进程池主循环
 * @param pool 进程池指针
 * @return 0成功，-1失败
 */
int process_pool_run(process_pool_t* pool);

/**
 * 停止进程池
 * @param pool 进程池指针
 */
void process_pool_stop(process_pool_t* pool);

/**
 * 销毁进程池
 * @param pool 进程池指针
 */
void process_pool_destroy(process_pool_t* pool);

/**
 * 获取进程池状态信息
 * @param pool 进程池指针
 * @param active_workers 活跃工作进程数
 * @param pending_tasks 待处理任务数
 */
void process_pool_get_status(process_pool_t* pool, int* active_workers, int* pending_tasks);

/* 工具函数 */

/**
 * 日志输出函数
 * @param level 日志级别
 * @param format 格式字符串
 */
void log_message(const char* level, const char* format, ...);

/**
 * 设置非阻塞模式
 * @param fd 文件描述符
 * @return 0成功，-1失败
 */
int set_nonblocking(int fd);

/**
 * 安全的读取数据
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 读取字节数
 * @return 实际读取字节数，-1失败
 */
ssize_t safe_read(int fd, void* buf, size_t count);

/**
 * 安全的写入数据
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 写入字节数
 * @return 实际写入字节数，-1失败
 */
ssize_t safe_write(int fd, const void* buf, size_t count);

/**
 * 创建管道并设置为非阻塞模式
 * @param pipefd 管道文件描述符数组
 * @return 0成功，-1失败
 */
int create_pipe_pair(int pipefd[2]);

/**
 * 关闭管道
 * @param pipefd 管道文件描述符数组
 */
void close_pipe_pair(int pipefd[2]);

/**
 * 创建工作进程
 * @param worker 工作进程结构体指针
 * @param handler 任务处理函数
 * @return 0成功，-1失败
 */
int create_worker_process(worker_process_t* worker, task_handler_t handler);

/**
 * 终止工作进程
 * @param worker 工作进程结构体指针
 */
void terminate_worker_process(worker_process_t* worker);

#endif /* PROCESS_POOL_H */