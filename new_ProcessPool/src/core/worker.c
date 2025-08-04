#include "../../include/internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <errno.h>
#include <sched.h>

// Worker进程状态
enum worker_state_internal {
    WORKER_INTERNAL_CREATED = 0,
    WORKER_INTERNAL_STARTING = 1,
    WORKER_INTERNAL_RUNNING = 2,
    WORKER_INTERNAL_STOPPING = 3,
    WORKER_INTERNAL_STOPPED = 4,
    WORKER_INTERNAL_ERROR = 5
};

// Worker控制命令
enum worker_command {
    WORKER_CMD_SHUTDOWN = 1,
    WORKER_CMD_PAUSE = 2,
    WORKER_CMD_RESUME = 3,
    WORKER_CMD_PING = 4
};

// ============================================================================
// Worker进程内部函数
// ============================================================================

static int default_task_handler(const void* input_data, size_t input_size,
                               void** output_data, size_t* output_size,
                               void* user_context) {
    (void)user_context; // 避免未使用警告
    
    if (!input_data || input_size == 0 || !output_data || !output_size) {
        return -1;
    }
    
    // 默认处理：简单的数据回显
    *output_data = malloc(input_size);
    if (!*output_data) {
        return -1;
    }
    
    memcpy(*output_data, input_data, input_size);
    *output_size = input_size;
    
    return 0;
}

static void worker_signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            // 优雅退出信号
            break;
        case SIGUSR1:
            // 用户自定义信号1
            break;
        case SIGUSR2:
            // 用户自定义信号2
            break;
        default:
            break;
    }
}

static int setup_worker_signals(void) {
    struct sigaction sa;
    
    // 设置信号处理器
    sa.sa_handler = worker_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    
    if (sigaction(SIGTERM, &sa, NULL) == -1 ||
        sigaction(SIGINT, &sa, NULL) == -1 ||
        sigaction(SIGUSR1, &sa, NULL) == -1 ||
        sigaction(SIGUSR2, &sa, NULL) == -1) {
        return -1;
    }
    
    // 忽略SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    
    return 0;
}

static int worker_process_task(worker_internal_t* worker, 
                              task_internal_t* task,
                              const pool_config_t* config) {
    if (!worker || !task || !config) {
        return -1;
    }
    
    // 更新任务状态
    ATOMIC_STORE(&task->state, TASK_STATE_RUNNING);
    ATOMIC_STORE(&task->worker_id, worker->worker_id);
    task->start_time_ns = get_time_ns();
    
    // 选择任务处理函数
    task_handler_t handler = task->desc.handler;
    if (!handler) {
        handler = config->default_handler;
    }
    if (!handler) {
        handler = default_task_handler;
    }
    
    // 执行任务
    void* output_data = NULL;
    size_t output_size = 0;
    
    int result = handler(task->input_data, task->input_size,
                        &output_data, &output_size,
                        config->user_context);
    
    task->end_time_ns = get_time_ns();
    
    if (result == 0) {
        // 任务成功完成
        task_set_result(task, output_data, output_size);
        ATOMIC_STORE(&task->state, TASK_STATE_COMPLETED);
    } else {
        // 任务执行失败
        task_set_error(task, result, "Task execution failed");
        ATOMIC_STORE(&task->state, TASK_STATE_FAILED);
    }
    
    // 清理输出数据
    if (output_data) {
        free(output_data);
    }
    
    // 更新统计信息
    ATOMIC_ADD(&worker->tasks_processed, 1);
    ATOMIC_STORE(&worker->last_heartbeat, get_time_ns());
    
    return result;
}

static void* worker_main_loop(void* arg) {
    worker_internal_t* worker = (worker_internal_t*)arg;
    if (!worker) {
        return NULL;
    }
    
    // 设置线程名称
    char thread_name[16];
    snprintf(thread_name, sizeof(thread_name), "worker-%u", worker->worker_id);
    pthread_setname_np(pthread_self(), thread_name);
    
    // 设置信号处理
    if (setup_worker_signals() != 0) {
        log_message(NULL, 0, "Worker %u: Failed to setup signals", worker->worker_id);
        return NULL;
    }
    
    // 设置进程名称
    prctl(PR_SET_NAME, thread_name, 0, 0, 0);
    
    log_message(NULL, 2, "Worker %u: Started main loop", worker->worker_id);
    
    // 创建epoll实例
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1) {
        log_message(NULL, 0, "Worker %u: Failed to create epoll", worker->worker_id);
        return NULL;
    }
    
    // 添加事件文件描述符到epoll
    struct epoll_event ev;
    
    // 任务通知eventfd
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = worker->task_eventfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, worker->task_eventfd, &ev) == -1) {
        log_message(NULL, 0, "Worker %u: Failed to add task eventfd to epoll", worker->worker_id);
        close(epoll_fd);
        return NULL;
    }
    
    // 控制命令eventfd
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = worker->control_eventfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, worker->control_eventfd, &ev) == -1) {
        log_message(NULL, 0, "Worker %u: Failed to add control eventfd to epoll", worker->worker_id);
        close(epoll_fd);
        return NULL;
    }
    
    // 主事件循环
    struct epoll_event events[8];
    bool running = true;
    
    while (running && ATOMIC_LOAD(&worker->state) == WORKER_INTERNAL_RUNNING) {
        int nfds = epoll_wait(epoll_fd, events, 8, 1000); // 1秒超时
        
        if (nfds == -1) {
            if (errno == EINTR) {
                continue; // 被信号中断，继续
            }
            log_message(NULL, 0, "Worker %u: epoll_wait failed: %s", 
                       worker->worker_id, strerror(errno));
            break;
        }
        
        if (nfds == 0) {
            // 超时，发送心跳
            ATOMIC_STORE(&worker->last_heartbeat, get_time_ns());
            continue;
        }
        
        // 处理事件
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            
            if (fd == worker->task_eventfd) {
                // 有新任务
                uint64_t value;
                if (read(worker->task_eventfd, &value, sizeof(value)) > 0) {
                    // 从共享内存读取任务
                    // TODO: 实现从共享内存队列读取任务的逻辑
                    log_message(NULL, 3, "Worker %u: Received task notification", 
                               worker->worker_id);
                }
            } else if (fd == worker->control_eventfd) {
                // 控制命令
                uint64_t cmd;
                if (read(worker->control_eventfd, &cmd, sizeof(cmd)) > 0) {
                    switch (cmd) {
                        case WORKER_CMD_SHUTDOWN:
                            log_message(NULL, 2, "Worker %u: Received shutdown command", 
                                       worker->worker_id);
                            running = false;
                            break;
                        case WORKER_CMD_PAUSE:
                            log_message(NULL, 2, "Worker %u: Received pause command", 
                                       worker->worker_id);
                            // TODO: 实现暂停逻辑
                            break;
                        case WORKER_CMD_RESUME:
                            log_message(NULL, 2, "Worker %u: Received resume command", 
                                       worker->worker_id);
                            // TODO: 实现恢复逻辑
                            break;
                        case WORKER_CMD_PING:
                            // 心跳响应
                            ATOMIC_STORE(&worker->last_heartbeat, get_time_ns());
                            break;
                        default:
                            log_message(NULL, 1, "Worker %u: Unknown command: %lu", 
                                       worker->worker_id, cmd);
                            break;
                    }
                }
            }
        }
    }
    
    close(epoll_fd);
    
    log_message(NULL, 2, "Worker %u: Main loop exited", worker->worker_id);
    
    return NULL;
}

// ============================================================================
// Worker管理函数实现
// ============================================================================

pool_error_t worker_create(process_pool_t* pool, uint32_t worker_id) {
    if (!pool || worker_id >= pool->config.max_workers) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    worker_internal_t* worker = &pool->workers[worker_id];
    memset(worker, 0, sizeof(worker_internal_t));
    
    worker->worker_id = worker_id;
    ATOMIC_STORE(&worker->state, WORKER_INTERNAL_CREATED);
    
    // 创建eventfd用于通信
    worker->task_eventfd = create_eventfd();
    if (worker->task_eventfd == -1) {
        log_message(pool, 0, "Failed to create task eventfd for worker %u", worker_id);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    worker->result_eventfd = create_eventfd();
    if (worker->result_eventfd == -1) {
        close(worker->task_eventfd);
        log_message(pool, 0, "Failed to create result eventfd for worker %u", worker_id);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    worker->control_eventfd = create_eventfd();
    if (worker->control_eventfd == -1) {
        close(worker->result_eventfd);
        close(worker->task_eventfd);
        log_message(pool, 0, "Failed to create control eventfd for worker %u", worker_id);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    // 创建共享内存
    snprintf(worker->shm_name, sizeof(worker->shm_name), 
             "/pool_%s_worker_%u", pool->config.pool_name, worker_id);
    
    worker->shared_mem_size = sizeof(shared_memory_t) + pool->config.queue_size * MAX_TASK_DATA_SIZE;
    worker->shared_mem = shm_create(worker->shm_name, worker->shared_mem_size);
    if (!worker->shared_mem) {
        close(worker->control_eventfd);
        close(worker->result_eventfd);
        close(worker->task_eventfd);
        log_message(pool, 0, "Failed to create shared memory for worker %u", worker_id);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    // 初始化共享内存
    ATOMIC_STORE(&worker->shared_mem->producer_pos, 0);
    ATOMIC_STORE(&worker->shared_mem->consumer_pos, 0);
    worker->shared_mem->queue_size = pool->config.queue_size;
    ATOMIC_STORE(&worker->shared_mem->total_submitted, 0);
    ATOMIC_STORE(&worker->shared_mem->total_completed, 0);
    ATOMIC_STORE(&worker->shared_mem->total_failed, 0);
    
    // 初始化统计信息
    ATOMIC_STORE(&worker->tasks_processed, 0);
    ATOMIC_STORE(&worker->last_heartbeat, get_time_ns());
    ATOMIC_STORE(&worker->current_task_id, 0);
    
    log_message(pool, 3, "Worker %u created successfully", worker_id);
    
    return POOL_SUCCESS;
}

pool_error_t worker_start(worker_internal_t* worker) {
    if (!worker) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    if (ATOMIC_LOAD(&worker->state) != WORKER_INTERNAL_CREATED) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    ATOMIC_STORE(&worker->state, WORKER_INTERNAL_STARTING);
    
    // 创建Worker进程
    pid_t pid = fork();
    if (pid == -1) {
        ATOMIC_STORE(&worker->state, WORKER_INTERNAL_ERROR);
        log_message(NULL, 0, "Failed to fork worker %u: %s", 
                   worker->worker_id, strerror(errno));
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    if (pid == 0) {
        // 子进程：Worker进程
        
        // 设置进程组
        if (setpgid(0, 0) == -1) {
            log_message(NULL, 1, "Worker %u: Failed to set process group", 
                       worker->worker_id);
        }
        
        // 设置进程优先级
        if (nice(0) == -1) {
            log_message(NULL, 1, "Worker %u: Failed to set nice value", 
                       worker->worker_id);
        }
        
        // 启动Worker主循环
        worker_main_loop(worker);
        
        // Worker进程退出
        log_message(NULL, 2, "Worker %u: Process exiting", worker->worker_id);
        _exit(0);
    } else {
        // 父进程：Master进程
        worker->pid = pid;
        ATOMIC_STORE(&worker->state, WORKER_INTERNAL_RUNNING);
        
        // 启动监控线程
        worker->monitor_running = true;
        if (pthread_create(&worker->monitor_thread, NULL, 
                          worker_monitor_thread, worker) != 0) {
            log_message(NULL, 1, "Failed to create monitor thread for worker %u", 
                       worker->worker_id);
            // 不是致命错误，继续运行
        }
        
        log_message(NULL, 2, "Worker %u started with PID %d", 
                   worker->worker_id, pid);
    }
    
    return POOL_SUCCESS;
}

pool_error_t worker_stop(worker_internal_t* worker, uint32_t timeout_ms) {
    if (!worker || worker->pid <= 0) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    int current_state = ATOMIC_LOAD(&worker->state);
    if (current_state != WORKER_INTERNAL_RUNNING) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    ATOMIC_STORE(&worker->state, WORKER_INTERNAL_STOPPING);
    
    log_message(NULL, 2, "Stopping worker %u (PID %d)", 
               worker->worker_id, worker->pid);
    
    // 发送关闭命令
    uint64_t cmd = WORKER_CMD_SHUTDOWN;
    if (write(worker->control_eventfd, &cmd, sizeof(cmd)) == -1) {
        log_message(NULL, 1, "Failed to send shutdown command to worker %u", 
                   worker->worker_id);
    }
    
    // 等待进程退出
    uint64_t start_time = get_time_ns();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    
    int status;
    while (get_time_ns() - start_time < timeout_ns) {
        pid_t result = waitpid(worker->pid, &status, WNOHANG);
        if (result == worker->pid) {
            // 进程已退出
            log_message(NULL, 2, "Worker %u exited normally", worker->worker_id);
            break;
        } else if (result == -1) {
            if (errno == ECHILD) {
                // 进程已经被回收
                break;
            }
            log_message(NULL, 1, "waitpid failed for worker %u: %s", 
                       worker->worker_id, strerror(errno));
            break;
        }
        
        usleep(10000); // 10ms
    }
    
    // 如果进程仍在运行，强制终止
    if (kill(worker->pid, 0) == 0) {
        log_message(NULL, 1, "Force killing worker %u", worker->worker_id);
        kill(worker->pid, SIGKILL);
        waitpid(worker->pid, &status, 0);
    }
    
    // 停止监控线程
    worker->monitor_running = false;
    if (worker->monitor_thread) {
        pthread_join(worker->monitor_thread, NULL);
        worker->monitor_thread = 0;
    }
    
    ATOMIC_STORE(&worker->state, WORKER_INTERNAL_STOPPED);
    
    return POOL_SUCCESS;
}

void worker_destroy(worker_internal_t* worker) {
    if (!worker) return;
    
    log_message(NULL, 3, "Destroying worker %u", worker->worker_id);
    
    // 确保Worker已停止
    if (ATOMIC_LOAD(&worker->state) == WORKER_INTERNAL_RUNNING) {
        worker_stop(worker, 5000);
    }
    
    // 关闭文件描述符
    if (worker->task_eventfd >= 0) {
        close(worker->task_eventfd);
        worker->task_eventfd = -1;
    }
    
    if (worker->result_eventfd >= 0) {
        close(worker->result_eventfd);
        worker->result_eventfd = -1;
    }
    
    if (worker->control_eventfd >= 0) {
        close(worker->control_eventfd);
        worker->control_eventfd = -1;
    }
    
    // 销毁共享内存
    if (worker->shared_mem) {
        shm_destroy(worker->shared_mem, worker->shm_name, worker->shared_mem_size);
        worker->shared_mem = NULL;
    }
    
    // 清零结构
    memset(worker, 0, sizeof(worker_internal_t));
}

bool worker_is_alive(worker_internal_t* worker) {
    if (!worker || worker->pid <= 0) {
        return false;
    }
    
    // 检查进程是否存在
    if (kill(worker->pid, 0) == -1) {
        if (errno == ESRCH) {
            return false; // 进程不存在
        }
    }
    
    // 检查心跳
    uint64_t now = get_time_ns();
    uint64_t last_heartbeat = ATOMIC_LOAD(&worker->last_heartbeat);
    uint64_t heartbeat_timeout = WORKER_HEARTBEAT_INTERVAL * 2 * 1000000000ULL; // 2倍心跳间隔
    
    return (now - last_heartbeat) < heartbeat_timeout;
}

pool_error_t worker_send_task(worker_internal_t* worker, task_internal_t* task) {
    if (!worker || !task) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    if (!worker_is_alive(worker)) {
        return POOL_ERROR_WORKER_DEAD;
    }
    
    // TODO: 实现将任务写入共享内存队列的逻辑
    
    // 通知Worker有新任务
    uint64_t value = 1;
    if (write(worker->task_eventfd, &value, sizeof(value)) == -1) {
        log_message(NULL, 0, "Failed to notify worker %u of new task", 
                   worker->worker_id);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    return POOL_SUCCESS;
}

pool_error_t worker_get_result(worker_internal_t* worker, task_internal_t* task) {
    if (!worker || !task) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    // TODO: 实现从共享内存读取结果的逻辑
    
    return POOL_SUCCESS;
}

// ============================================================================
// Worker监控线程
// ============================================================================

void* worker_monitor_thread(void* arg) {
    worker_internal_t* worker = (worker_internal_t*)arg;
    if (!worker) {
        return NULL;
    }
    
    char thread_name[16];
    snprintf(thread_name, sizeof(thread_name), "monitor-%u", worker->worker_id);
    pthread_setname_np(pthread_self(), thread_name);
    
    log_message(NULL, 3, "Monitor thread started for worker %u", worker->worker_id);
    
    while (worker->monitor_running) {
        // 检查Worker进程状态
        if (!worker_is_alive(worker)) {
            log_message(NULL, 1, "Worker %u is dead, marking for restart", 
                       worker->worker_id);
            ATOMIC_STORE(&worker->state, WORKER_INTERNAL_ERROR);
            break;
        }
        
        // 发送心跳ping
        uint64_t cmd = WORKER_CMD_PING;
        if (write(worker->control_eventfd, &cmd, sizeof(cmd)) == -1) {
            log_message(NULL, 1, "Failed to send ping to worker %u", 
                       worker->worker_id);
        }
        
        // 更新性能指标
        // TODO: 实现CPU和内存使用率统计
        
        sleep(WORKER_HEARTBEAT_INTERVAL);
    }
    
    log_message(NULL, 3, "Monitor thread exited for worker %u", worker->worker_id);
    
    return NULL;
}