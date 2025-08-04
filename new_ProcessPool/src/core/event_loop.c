#include "../../include/internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <signal.h>

// ============================================================================
// 事件类型定义
// ============================================================================

typedef enum {
    EVENT_TYPE_TASK_SUBMIT = 1,     // 任务提交事件
    EVENT_TYPE_TASK_COMPLETE = 2,   // 任务完成事件
    EVENT_TYPE_WORKER_STATUS = 3,   // Worker状态变化事件
    EVENT_TYPE_TIMER = 4,           // 定时器事件
    EVENT_TYPE_SIGNAL = 5,          // 信号事件
    EVENT_TYPE_CONTROL = 6          // 控制命令事件
} event_type_t;

// 事件数据结构
typedef struct {
    event_type_t type;
    int fd;
    void* data;
    uint32_t events;
} event_data_t;

// ============================================================================
// 事件循环状态
// ============================================================================

typedef struct {
    int epoll_fd;
    int task_submit_eventfd;
    int control_eventfd;
    int signal_fd;
    int timer_fd;
    bool running;
    pthread_t thread;
    process_pool_t* pool;
    
    // 统计信息
    _Atomic uint64_t events_processed;
    _Atomic uint64_t tasks_submitted;
    _Atomic uint64_t tasks_completed;
    _Atomic uint64_t worker_events;
    _Atomic uint64_t timer_events;
} event_loop_t;

static event_loop_t g_event_loop = {0};

// ============================================================================
// 辅助函数
// ============================================================================

static int setup_signal_fd(void) {
    sigset_t mask;
    int sfd;
    
    // 设置信号掩码
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);  // 子进程退出
    sigaddset(&mask, SIGTERM);  // 终止信号
    sigaddset(&mask, SIGINT);   // 中断信号
    sigaddset(&mask, SIGUSR1);  // 用户信号1
    sigaddset(&mask, SIGUSR2);  // 用户信号2
    
    // 阻塞这些信号，通过signalfd处理
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) == -1) {
        return -1;
    }
    
    // 创建signalfd
    sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd == -1) {
        return -1;
    }
    
    return sfd;
}

static int setup_timer_fd(void) {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (tfd == -1) {
        return -1;
    }
    
    // 设置定时器：每秒触发一次
    struct itimerspec timer_spec;
    timer_spec.it_value.tv_sec = 1;
    timer_spec.it_value.tv_nsec = 0;
    timer_spec.it_interval.tv_sec = 1;
    timer_spec.it_interval.tv_nsec = 0;
    
    if (timerfd_settime(tfd, 0, &timer_spec, NULL) == -1) {
        close(tfd);
        return -1;
    }
    
    return tfd;
}

static int add_epoll_event(int epoll_fd, int fd, uint32_t events, event_type_t type, void* data) {
    struct epoll_event ev;
    event_data_t* event_data = malloc(sizeof(event_data_t));
    if (!event_data) {
        return -1;
    }
    
    event_data->type = type;
    event_data->fd = fd;
    event_data->data = data;
    event_data->events = events;
    
    ev.events = events;
    ev.data.ptr = event_data;
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        free(event_data);
        return -1;
    }
    
    return 0;
}

static void remove_epoll_event(int epoll_fd, int fd) {
    struct epoll_event ev;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev) == 0) {
        if (ev.data.ptr) {
            free(ev.data.ptr);
        }
    }
}

// ============================================================================
// 事件处理函数
// ============================================================================

static void handle_task_submit_event(event_loop_t* loop) {
    uint64_t value;
    
    // 读取eventfd值
    if (read(loop->task_submit_eventfd, &value, sizeof(value)) == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_message(loop->pool, 0, "Failed to read task submit eventfd: %s", 
                       strerror(errno));
        }
        return;
    }
    
    log_message(loop->pool, 3, "Received %lu task submit notifications", value);
    
    // 处理提交的任务
    for (uint64_t i = 0; i < value; i++) {
        // 从任务队列中取出任务
        task_internal_t* task = NULL;
        if (queue_dequeue(loop->pool->task_queue, (void**)&task) == 0) {
            // 分配任务给Worker
            pool_error_t result = assign_task_to_worker(loop->pool, task);
            if (result != POOL_SUCCESS) {
                log_message(loop->pool, 1, "Failed to assign task %lu to worker: %d", 
                           task->task_id, result);
                
                // 标记任务失败
                task_set_error(task, result, "Failed to assign to worker");
                ATOMIC_STORE(&task->state, TASK_STATE_FAILED);
                
                // 通知等待的线程
                pthread_mutex_lock(&task->mutex);
                pthread_cond_broadcast(&task->completion_cond);
                pthread_mutex_unlock(&task->mutex);
            }
        }
    }
    
    ATOMIC_ADD(&loop->tasks_submitted, value);
}

static void handle_task_complete_event(event_loop_t* loop, int worker_id) {
    uint64_t value;
    worker_internal_t* worker = &loop->pool->workers[worker_id];
    
    // 读取eventfd值
    if (read(worker->result_eventfd, &value, sizeof(value)) == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_message(loop->pool, 0, "Failed to read result eventfd from worker %d: %s", 
                       worker_id, strerror(errno));
        }
        return;
    }
    
    log_message(loop->pool, 3, "Worker %d completed %lu tasks", worker_id, value);
    
    // 处理完成的任务
    for (uint64_t i = 0; i < value; i++) {
        // 从Worker的结果队列中取出任务结果
        // TODO: 实现从共享内存读取任务结果的逻辑
        
        // 更新统计信息
        ATOMIC_ADD(&loop->pool->stats.tasks_completed, 1);
    }
    
    ATOMIC_ADD(&loop->tasks_completed, value);
}

static void handle_worker_status_event(event_loop_t* loop, int worker_id) {
    worker_internal_t* worker = &loop->pool->workers[worker_id];
    
    log_message(loop->pool, 3, "Worker %d status changed", worker_id);
    
    // 检查Worker状态
    if (!worker_is_alive(worker)) {
        log_message(loop->pool, 1, "Worker %d is dead, attempting restart", worker_id);
        
        // 重启Worker
        worker_stop(worker, 1000);
        worker_destroy(worker);
        
        if (worker_create(loop->pool, worker_id) == POOL_SUCCESS) {
            if (worker_start(worker) == POOL_SUCCESS) {
                log_message(loop->pool, 2, "Worker %d restarted successfully", worker_id);
                
                // 重新添加Worker事件到epoll
                add_epoll_event(loop->epoll_fd, worker->result_eventfd, 
                               EPOLLIN | EPOLLET, EVENT_TYPE_TASK_COMPLETE, 
                               (void*)(intptr_t)worker_id);
            } else {
                log_message(loop->pool, 0, "Failed to restart worker %d", worker_id);
            }
        }
    }
    
    ATOMIC_ADD(&loop->worker_events, 1);
}

static void handle_timer_event(event_loop_t* loop) {
    uint64_t expirations;
    
    // 读取定时器过期次数
    if (read(loop->timer_fd, &expirations, sizeof(expirations)) == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_message(loop->pool, 0, "Failed to read timer fd: %s", strerror(errno));
        }
        return;
    }
    
    log_message(loop->pool, 4, "Timer expired %lu times", expirations);
    
    // 执行定期任务
    
    // 1. 更新统计信息
    update_pool_statistics(loop->pool);
    
    // 2. 检查Worker健康状态
    for (uint32_t i = 0; i < loop->pool->config.max_workers; i++) {
        worker_internal_t* worker = &loop->pool->workers[i];
        if (ATOMIC_LOAD(&worker->state) == WORKER_STATE_RUNNING) {
            if (!worker_is_alive(worker)) {
                log_message(loop->pool, 1, "Worker %u health check failed", i);
                handle_worker_status_event(loop, i);
            }
        }
    }
    
    // 3. 清理过期任务
    cleanup_expired_tasks(loop->pool);
    
    // 4. 动态调整Worker数量
    if (loop->pool->config.enable_auto_scaling) {
        adjust_worker_count(loop->pool);
    }
    
    ATOMIC_ADD(&loop->timer_events, expirations);
}

static void handle_signal_event(event_loop_t* loop) {
    struct signalfd_siginfo si;
    
    while (read(loop->signal_fd, &si, sizeof(si)) == sizeof(si)) {
        switch (si.ssi_signo) {
            case SIGCHLD:
                log_message(loop->pool, 3, "Received SIGCHLD from PID %d", si.ssi_pid);
                
                // 查找对应的Worker
                for (uint32_t i = 0; i < loop->pool->config.max_workers; i++) {
                    worker_internal_t* worker = &loop->pool->workers[i];
                    if (worker->pid == si.ssi_pid) {
                        log_message(loop->pool, 2, "Worker %u (PID %d) exited with status %d", 
                                   i, si.ssi_pid, si.ssi_status);
                        handle_worker_status_event(loop, i);
                        break;
                    }
                }
                break;
                
            case SIGTERM:
            case SIGINT:
                log_message(loop->pool, 2, "Received termination signal %d", si.ssi_signo);
                loop->running = false;
                break;
                
            case SIGUSR1:
                log_message(loop->pool, 2, "Received SIGUSR1, dumping statistics");
                dump_pool_statistics(loop->pool);
                break;
                
            case SIGUSR2:
                log_message(loop->pool, 2, "Received SIGUSR2, toggling debug mode");
                toggle_debug_mode(loop->pool);
                break;
                
            default:
                log_message(loop->pool, 1, "Received unknown signal %d", si.ssi_signo);
                break;
        }
    }
}

static void handle_control_event(event_loop_t* loop) {
    uint64_t command;
    
    // 读取控制命令
    if (read(loop->control_eventfd, &command, sizeof(command)) == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_message(loop->pool, 0, "Failed to read control eventfd: %s", 
                       strerror(errno));
        }
        return;
    }
    
    log_message(loop->pool, 3, "Received control command: %lu", command);
    
    switch (command) {
        case 1: // 停止事件循环
            log_message(loop->pool, 2, "Received stop command");
            loop->running = false;
            break;
            
        case 2: // 重新加载配置
            log_message(loop->pool, 2, "Received reload command");
            // TODO: 实现配置重新加载
            break;
            
        case 3: // 强制垃圾回收
            log_message(loop->pool, 2, "Received GC command");
            force_garbage_collection(loop->pool);
            break;
            
        default:
            log_message(loop->pool, 1, "Unknown control command: %lu", command);
            break;
    }
}

// ============================================================================
// 事件循环主函数
// ============================================================================

static void* event_loop_thread(void* arg) {
    event_loop_t* loop = (event_loop_t*)arg;
    
    pthread_setname_np(pthread_self(), "event-loop");
    
    log_message(loop->pool, 2, "Event loop thread started");
    
    struct epoll_event events[EPOLL_MAX_EVENTS];
    
    while (loop->running) {
        int nfds = epoll_wait(loop->epoll_fd, events, EPOLL_MAX_EVENTS, 1000);
        
        if (nfds == -1) {
            if (errno == EINTR) {
                continue; // 被信号中断，继续
            }
            log_message(loop->pool, 0, "epoll_wait failed: %s", strerror(errno));
            break;
        }
        
        if (nfds == 0) {
            // 超时，执行定期检查
            continue;
        }
        
        // 处理事件
        for (int i = 0; i < nfds; i++) {
            event_data_t* event_data = (event_data_t*)events[i].data.ptr;
            if (!event_data) {
                continue;
            }
            
            switch (event_data->type) {
                case EVENT_TYPE_TASK_SUBMIT:
                    handle_task_submit_event(loop);
                    break;
                    
                case EVENT_TYPE_TASK_COMPLETE:
                    handle_task_complete_event(loop, (int)(intptr_t)event_data->data);
                    break;
                    
                case EVENT_TYPE_WORKER_STATUS:
                    handle_worker_status_event(loop, (int)(intptr_t)event_data->data);
                    break;
                    
                case EVENT_TYPE_TIMER:
                    handle_timer_event(loop);
                    break;
                    
                case EVENT_TYPE_SIGNAL:
                    handle_signal_event(loop);
                    break;
                    
                case EVENT_TYPE_CONTROL:
                    handle_control_event(loop);
                    break;
                    
                default:
                    log_message(loop->pool, 1, "Unknown event type: %d", event_data->type);
                    break;
            }
            
            ATOMIC_ADD(&loop->events_processed, 1);
        }
    }
    
    log_message(loop->pool, 2, "Event loop thread exited");
    
    return NULL;
}

// ============================================================================
// 公共接口实现
// ============================================================================

pool_error_t event_loop_init(process_pool_t* pool) {
    if (!pool) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    memset(&g_event_loop, 0, sizeof(g_event_loop));
    g_event_loop.pool = pool;
    
    // 创建epoll实例
    g_event_loop.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (g_event_loop.epoll_fd == -1) {
        log_message(pool, 0, "Failed to create epoll instance: %s", strerror(errno));
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    // 创建任务提交eventfd
    g_event_loop.task_submit_eventfd = create_eventfd();
    if (g_event_loop.task_submit_eventfd == -1) {
        close(g_event_loop.epoll_fd);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    // 创建控制eventfd
    g_event_loop.control_eventfd = create_eventfd();
    if (g_event_loop.control_eventfd == -1) {
        close(g_event_loop.task_submit_eventfd);
        close(g_event_loop.epoll_fd);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    // 创建信号fd
    g_event_loop.signal_fd = setup_signal_fd();
    if (g_event_loop.signal_fd == -1) {
        close(g_event_loop.control_eventfd);
        close(g_event_loop.task_submit_eventfd);
        close(g_event_loop.epoll_fd);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    // 创建定时器fd
    g_event_loop.timer_fd = setup_timer_fd();
    if (g_event_loop.timer_fd == -1) {
        close(g_event_loop.signal_fd);
        close(g_event_loop.control_eventfd);
        close(g_event_loop.task_submit_eventfd);
        close(g_event_loop.epoll_fd);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    // 添加事件到epoll
    if (add_epoll_event(g_event_loop.epoll_fd, g_event_loop.task_submit_eventfd,
                       EPOLLIN | EPOLLET, EVENT_TYPE_TASK_SUBMIT, NULL) == -1 ||
        add_epoll_event(g_event_loop.epoll_fd, g_event_loop.control_eventfd,
                       EPOLLIN | EPOLLET, EVENT_TYPE_CONTROL, NULL) == -1 ||
        add_epoll_event(g_event_loop.epoll_fd, g_event_loop.signal_fd,
                       EPOLLIN, EVENT_TYPE_SIGNAL, NULL) == -1 ||
        add_epoll_event(g_event_loop.epoll_fd, g_event_loop.timer_fd,
                       EPOLLIN, EVENT_TYPE_TIMER, NULL) == -1) {
        
        event_loop_cleanup();
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    // 保存eventfd到pool中
    pool->task_submit_eventfd = g_event_loop.task_submit_eventfd;
    pool->control_eventfd = g_event_loop.control_eventfd;
    
    log_message(pool, 2, "Event loop initialized successfully");
    
    return POOL_SUCCESS;
}

pool_error_t event_loop_start(void) {
    if (g_event_loop.epoll_fd == -1) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    g_event_loop.running = true;
    
    if (pthread_create(&g_event_loop.thread, NULL, event_loop_thread, &g_event_loop) != 0) {
        g_event_loop.running = false;
        log_message(g_event_loop.pool, 0, "Failed to create event loop thread");
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    log_message(g_event_loop.pool, 2, "Event loop started");
    
    return POOL_SUCCESS;
}

pool_error_t event_loop_stop(void) {
    if (!g_event_loop.running) {
        return POOL_SUCCESS;
    }
    
    log_message(g_event_loop.pool, 2, "Stopping event loop");
    
    // 发送停止命令
    uint64_t command = 1;
    if (write(g_event_loop.control_eventfd, &command, sizeof(command)) == -1) {
        log_message(g_event_loop.pool, 1, "Failed to send stop command to event loop");
    }
    
    // 等待线程退出
    if (g_event_loop.thread) {
        pthread_join(g_event_loop.thread, NULL);
        g_event_loop.thread = 0;
    }
    
    g_event_loop.running = false;
    
    log_message(g_event_loop.pool, 2, "Event loop stopped");
    
    return POOL_SUCCESS;
}

void event_loop_cleanup(void) {
    if (g_event_loop.running) {
        event_loop_stop();
    }
    
    if (g_event_loop.epoll_fd >= 0) {
        close(g_event_loop.epoll_fd);
        g_event_loop.epoll_fd = -1;
    }
    
    if (g_event_loop.task_submit_eventfd >= 0) {
        close(g_event_loop.task_submit_eventfd);
        g_event_loop.task_submit_eventfd = -1;
    }
    
    if (g_event_loop.control_eventfd >= 0) {
        close(g_event_loop.control_eventfd);
        g_event_loop.control_eventfd = -1;
    }
    
    if (g_event_loop.signal_fd >= 0) {
        close(g_event_loop.signal_fd);
        g_event_loop.signal_fd = -1;
    }
    
    if (g_event_loop.timer_fd >= 0) {
        close(g_event_loop.timer_fd);
        g_event_loop.timer_fd = -1;
    }
    
    memset(&g_event_loop, 0, sizeof(g_event_loop));
}

pool_error_t event_loop_notify_task_submit(void) {
    if (g_event_loop.task_submit_eventfd == -1) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    uint64_t value = 1;
    if (write(g_event_loop.task_submit_eventfd, &value, sizeof(value)) == -1) {
        log_message(g_event_loop.pool, 0, "Failed to notify task submit: %s", 
                   strerror(errno));
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    return POOL_SUCCESS;
}

pool_error_t event_loop_add_worker_events(uint32_t worker_id) {
    if (worker_id >= g_event_loop.pool->config.max_workers) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    worker_internal_t* worker = &g_event_loop.pool->workers[worker_id];
    
    // 添加任务完成事件
    if (add_epoll_event(g_event_loop.epoll_fd, worker->result_eventfd,
                       EPOLLIN | EPOLLET, EVENT_TYPE_TASK_COMPLETE,
                       (void*)(intptr_t)worker_id) == -1) {
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    return POOL_SUCCESS;
}

pool_error_t event_loop_remove_worker_events(uint32_t worker_id) {
    if (worker_id >= g_event_loop.pool->config.max_workers) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    worker_internal_t* worker = &g_event_loop.pool->workers[worker_id];
    
    // 移除任务完成事件
    remove_epoll_event(g_event_loop.epoll_fd, worker->result_eventfd);
    
    return POOL_SUCCESS;
}

void event_loop_get_stats(uint64_t* events_processed,
                         uint64_t* tasks_submitted,
                         uint64_t* tasks_completed,
                         uint64_t* worker_events,
                         uint64_t* timer_events) {
    if (events_processed) {
        *events_processed = ATOMIC_LOAD(&g_event_loop.events_processed);
    }
    
    if (tasks_submitted) {
        *tasks_submitted = ATOMIC_LOAD(&g_event_loop.tasks_submitted);
    }
    
    if (tasks_completed) {
        *tasks_completed = ATOMIC_LOAD(&g_event_loop.tasks_completed);
    }
    
    if (worker_events) {
        *worker_events = ATOMIC_LOAD(&g_event_loop.worker_events);
    }
    
    if (timer_events) {
        *timer_events = ATOMIC_LOAD(&g_event_loop.timer_events);
    }
}