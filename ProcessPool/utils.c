#include "process_pool.h"
#include <stdarg.h>
#include <sys/time.h>

/**
 * 日志输出函数
 */
void log_message(const char* level, const char* format, ...) {
    struct timeval tv;
    struct tm* tm_info;
    char timestamp[64];
    
    gettimeofday(&tv, NULL);
    tm_info = localtime(&tv.tv_sec);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(stderr, "[%s.%03ld] [%s] ", timestamp, tv.tv_usec / 1000, level);
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
    fflush(stderr);
}

/**
 * 设置文件描述符为非阻塞模式
 */
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        log_message("ERROR", "fcntl F_GETFL failed: %s", strerror(errno));
        return -1;
    }
    
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        log_message("ERROR", "fcntl F_SETFL failed: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

/**
 * 安全的读取数据，处理EINTR和部分读取
 */
ssize_t safe_read(int fd, void* buf, size_t count) {
    ssize_t total_read = 0;
    ssize_t bytes_read;
    char* ptr = (char*)buf;
    
    while (total_read < count) {
        bytes_read = read(fd, ptr + total_read, count - total_read);
        
        if (bytes_read == -1) {
            if (errno == EINTR) {
                continue;  /* 被信号中断，重试 */
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;     /* 非阻塞模式下没有数据可读 */
            } else {
                log_message("ERROR", "read failed: %s", strerror(errno));
                return -1;
            }
        } else if (bytes_read == 0) {
            break;  /* EOF */
        } else {
            total_read += bytes_read;
        }
    }
    
    return total_read;
}

/**
 * 安全的写入数据，处理EINTR和部分写入
 */
ssize_t safe_write(int fd, const void* buf, size_t count) {
    ssize_t total_written = 0;
    ssize_t bytes_written;
    const char* ptr = (const char*)buf;
    
    while (total_written < count) {
        bytes_written = write(fd, ptr + total_written, count - total_written);
        
        if (bytes_written == -1) {
            if (errno == EINTR) {
                continue;  /* 被信号中断，重试 */
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;     /* 非阻塞模式下无法写入更多数据 */
            } else {
                log_message("ERROR", "write failed: %s", strerror(errno));
                return -1;
            }
        } else {
            total_written += bytes_written;
        }
    }
    
    return total_written;
}

/**
 * 创建管道并设置为非阻塞模式
 */
int create_pipe_pair(int pipefd[2]) {
    if (pipe(pipefd) == -1) {
        log_message("ERROR", "pipe creation failed: %s", strerror(errno));
        return -1;
    }
    
    /* 设置读端为非阻塞 */
    if (set_nonblocking(pipefd[0]) == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    /* 写端保持阻塞模式，避免写入时丢失数据 */
    
    return 0;
}

/**
 * 关闭管道
 */
void close_pipe_pair(int pipefd[2]) {
    if (pipefd[0] != -1) {
        close(pipefd[0]);
        pipefd[0] = -1;
    }
    if (pipefd[1] != -1) {
        close(pipefd[1]);
        pipefd[1] = -1;
    }
}

/**
 * 发送任务到工作进程
 */
int send_task_to_worker(worker_process_t* worker, const task_t* task) {
    ssize_t bytes_written = safe_write(worker->pipe_to_worker[1], task, sizeof(task_t));
    if (bytes_written != sizeof(task_t)) {
        log_message("ERROR", "Failed to send task to worker %d", worker->pid);
        return -1;
    }
    
    worker->status = WORKER_BUSY;
    worker->current_task_id = task->task_id;
    worker->last_active = time(NULL);
    
    log_message("DEBUG", "Task %d sent to worker %d", task->task_id, worker->pid);
    return 0;
}

/**
 * 从工作进程接收结果
 */
int receive_result_from_worker(worker_process_t* worker, task_result_t* result) {
    ssize_t bytes_read = safe_read(worker->pipe_from_worker[0], result, sizeof(task_result_t));
    if (bytes_read != sizeof(task_result_t)) {
        if (bytes_read == 0) {
            log_message("WARN", "Worker %d pipe closed", worker->pid);
            worker->status = WORKER_DEAD;
        } else if (bytes_read > 0) {
            log_message("ERROR", "Partial result received from worker %d", worker->pid);
        }
        return -1;
    }
    
    worker->status = WORKER_IDLE;
    worker->current_task_id = -1;
    worker->last_active = time(NULL);
    
    log_message("DEBUG", "Result for task %d received from worker %d", 
                result->task_id, worker->pid);
    return 0;
}

/**
 * 检查工作进程是否存活
 */
int is_worker_alive(worker_process_t* worker) {
    if (worker->pid <= 0) {
        return 0;
    }
    
    /* 使用kill(pid, 0)检查进程是否存在 */
    if (kill(worker->pid, 0) == -1) {
        if (errno == ESRCH) {
            log_message("WARN", "Worker %d is dead", worker->pid);
            worker->status = WORKER_DEAD;
            return 0;
        }
    }
    
    return 1;
}

/**
 * 等待并回收死亡的子进程
 */
void reap_dead_children(process_pool_t* pool) {
    pid_t pid;
    int status;
    int i;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        log_message("INFO", "Child process %d exited with status %d", pid, status);
        
        /* 找到对应的工作进程并标记为死亡 */
        for (i = 0; i < pool->worker_count; i++) {
            if (pool->workers[i].pid == pid) {
                pool->workers[i].status = WORKER_DEAD;
                pool->workers[i].pid = -1;
                break;
            }
        }
    }
}