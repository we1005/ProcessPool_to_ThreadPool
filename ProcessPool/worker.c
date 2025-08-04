#include "process_pool.h"

/* 全局变量，用于信号处理 */
static volatile sig_atomic_t worker_should_exit = 0;

/**
 * 工作进程信号处理函数
 */
static void worker_signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            worker_should_exit = 1;
            break;
        case SIGPIPE:
            /* 忽略SIGPIPE，通过write返回值检测管道断开 */
            break;
        default:
            break;
    }
}

/**
 * 设置工作进程信号处理
 */
static int setup_worker_signals(void) {
    struct sigaction sa;
    
    /* 设置信号处理函数 */
    sa.sa_handler = worker_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  /* 自动重启被中断的系统调用 */
    
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        log_message("ERROR", "sigaction SIGTERM failed: %s", strerror(errno));
        return -1;
    }
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        log_message("ERROR", "sigaction SIGINT failed: %s", strerror(errno));
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
 * 默认任务处理函数示例
 * 这个函数会被工作进程调用来处理具体的任务
 */
int default_task_handler(const char* task_data, int data_len, char* result_data, int* result_len) {
    /* 示例：简单的字符串处理任务 */
    if (data_len <= 0 || data_len >= MAX_TASK_DATA) {
        return -1;
    }
    
    /* 模拟一些处理时间 */
    usleep(100000);  /* 100ms */
    
    /* 简单的处理：将输入转换为大写并添加前缀 */
    int prefix_len = snprintf(result_data, MAX_TASK_DATA, "PROCESSED: ");
    int remaining = MAX_TASK_DATA - prefix_len - 1;
    
    if (remaining <= 0) {
        return -1;
    }
    
    int copy_len = (data_len < remaining) ? data_len : remaining;
    for (int i = 0; i < copy_len; i++) {
        char c = task_data[i];
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
        result_data[prefix_len + i] = c;
    }
    
    *result_len = prefix_len + copy_len;
    result_data[*result_len] = '\0';
    
    return 0;  /* 成功 */
}

/**
 * 工作进程主函数
 * @param read_fd 从主进程读取任务的文件描述符
 * @param write_fd 向主进程写入结果的文件描述符
 * @param handler 任务处理函数
 */
void worker_main(int read_fd, int write_fd, task_handler_t handler) {
    task_t task;
    task_result_t result;
    fd_set read_fds;
    struct timeval timeout;
    int select_result;
    
    log_message("INFO", "Worker process %d started", getpid());
    
    /* 设置信号处理 */
    if (setup_worker_signals() == -1) {
        log_message("ERROR", "Failed to setup worker signals");
        exit(EXIT_FAILURE);
    }
    
    /* 如果没有提供处理函数，使用默认的 */
    if (handler == NULL) {
        handler = default_task_handler;
    }
    
    /* 工作进程主循环 */
    while (!worker_should_exit) {
        FD_ZERO(&read_fds);
        FD_SET(read_fd, &read_fds);
        
        /* 设置超时时间，定期检查退出标志 */
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        select_result = select(read_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (select_result == -1) {
            if (errno == EINTR) {
                continue;  /* 被信号中断，继续循环 */
            }
            log_message("ERROR", "Worker select failed: %s", strerror(errno));
            break;
        }
        
        if (select_result == 0) {
            /* 超时，继续循环检查退出标志 */
            continue;
        }
        
        if (FD_ISSET(read_fd, &read_fds)) {
            /* 有任务可读 */
            ssize_t bytes_read = safe_read(read_fd, &task, sizeof(task_t));
            
            if (bytes_read == 0) {
                /* 管道关闭，主进程可能已退出 */
                log_message("INFO", "Worker %d: pipe closed by master", getpid());
                break;
            }
            
            if (bytes_read != sizeof(task_t)) {
                if (bytes_read > 0) {
                    log_message("ERROR", "Worker %d: partial task received", getpid());
                }
                continue;
            }
            
            log_message("DEBUG", "Worker %d processing task %d", getpid(), task.task_id);
            
            /* 初始化结果结构 */
            memset(&result, 0, sizeof(result));
            result.task_id = task.task_id;
            
            /* 调用任务处理函数 */
            result.result_code = handler(task.data, task.data_len, 
                                       result.result_data, &result.result_len);
            
            /* 发送结果回主进程 */
            ssize_t bytes_written = safe_write(write_fd, &result, sizeof(task_result_t));
            if (bytes_written != sizeof(task_result_t)) {
                log_message("ERROR", "Worker %d: failed to send result", getpid());
                break;
            }
            
            log_message("DEBUG", "Worker %d completed task %d with result %d", 
                       getpid(), task.task_id, result.result_code);
        }
    }
    
    log_message("INFO", "Worker process %d exiting", getpid());
    close(read_fd);
    close(write_fd);
    exit(EXIT_SUCCESS);
}

/**
 * 创建工作进程
 * @param worker 工作进程结构体指针
 * @param handler 任务处理函数
 * @return 0成功，-1失败
 */
int create_worker_process(worker_process_t* worker, task_handler_t handler) {
    /* 创建主进程到工作进程的管道 */
    if (create_pipe_pair(worker->pipe_to_worker) == -1) {
        log_message("ERROR", "Failed to create pipe to worker");
        return -1;
    }
    
    /* 创建工作进程到主进程的管道 */
    if (create_pipe_pair(worker->pipe_from_worker) == -1) {
        log_message("ERROR", "Failed to create pipe from worker");
        close_pipe_pair(worker->pipe_to_worker);
        return -1;
    }
    
    /* 创建子进程 */
    worker->pid = fork();
    
    if (worker->pid == -1) {
        log_message("ERROR", "fork failed: %s", strerror(errno));
        close_pipe_pair(worker->pipe_to_worker);
        close_pipe_pair(worker->pipe_from_worker);
        return -1;
    }
    
    if (worker->pid == 0) {
        /* 子进程 */
        
        /* 关闭不需要的管道端 */
        close(worker->pipe_to_worker[1]);    /* 关闭写端 */
        close(worker->pipe_from_worker[0]);  /* 关闭读端 */
        
        /* 执行工作进程主函数 */
        worker_main(worker->pipe_to_worker[0], worker->pipe_from_worker[1], handler);
        
        /* 不应该到达这里 */
        exit(EXIT_FAILURE);
    } else {
        /* 父进程 */
        
        /* 关闭不需要的管道端 */
        close(worker->pipe_to_worker[0]);    /* 关闭读端 */
        close(worker->pipe_from_worker[1]);  /* 关闭写端 */
        
        /* 初始化工作进程状态 */
        worker->status = WORKER_IDLE;
        worker->last_active = time(NULL);
        worker->current_task_id = -1;
        
        log_message("INFO", "Created worker process %d", worker->pid);
        return 0;
    }
}

/**
 * 终止工作进程
 * @param worker 工作进程结构体指针
 */
void terminate_worker_process(worker_process_t* worker) {
    if (worker->pid > 0) {
        log_message("INFO", "Terminating worker process %d", worker->pid);
        
        /* 发送SIGTERM信号 */
        kill(worker->pid, SIGTERM);
        
        /* 等待进程退出，最多等待5秒 */
        int status;
        int wait_count = 0;
        while (wait_count < 50) {  /* 50 * 100ms = 5秒 */
            if (waitpid(worker->pid, &status, WNOHANG) == worker->pid) {
                break;
            }
            usleep(100000);  /* 100ms */
            wait_count++;
        }
        
        /* 如果进程仍未退出，强制杀死 */
        if (kill(worker->pid, 0) == 0) {
            log_message("WARN", "Force killing worker process %d", worker->pid);
            kill(worker->pid, SIGKILL);
            waitpid(worker->pid, &status, 0);
        }
        
        worker->pid = -1;
    }
    
    /* 关闭管道 */
    close_pipe_pair(worker->pipe_to_worker);
    close_pipe_pair(worker->pipe_from_worker);
    
    worker->status = WORKER_DEAD;
}