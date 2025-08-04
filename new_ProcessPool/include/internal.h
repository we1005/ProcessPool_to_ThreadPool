#ifndef INTERNAL_H
#define INTERNAL_H

#include "process_pool.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/mman.h>
#include <stdatomic.h>
#include <pthread.h>

// 内部配置常量
#define EPOLL_MAX_EVENTS 64
#define SHM_NAME_MAX_LEN 64
#define WORKER_HEARTBEAT_INTERVAL 5  // 秒
#define TASK_ID_INVALID 0
#define METRICS_UPDATE_INTERVAL 1    // 秒

// 原子操作宏
#define ATOMIC_LOAD(ptr) atomic_load(ptr)
#define ATOMIC_STORE(ptr, val) atomic_store(ptr, val)
#define ATOMIC_CAS(ptr, expected, desired) atomic_compare_exchange_weak(ptr, expected, desired)
#define ATOMIC_ADD(ptr, val) atomic_fetch_add(ptr, val)
#define ATOMIC_SUB(ptr, val) atomic_fetch_sub(ptr, val)

// 内存屏障
#define MEMORY_BARRIER() atomic_thread_fence(memory_order_seq_cst)

// 任务内部结构
typedef struct task_internal {
    uint64_t task_id;               // 任务ID
    task_desc_t desc;               // 任务描述
    void* input_data;               // 输入数据
    size_t input_size;              // 输入大小
    
    // 状态管理
    atomic_int state;               // 任务状态
    atomic_uint worker_id;          // 分配的worker ID
    
    // 时间戳
    uint64_t submit_time_ns;        // 提交时间
    uint64_t start_time_ns;         // 开始时间
    uint64_t end_time_ns;           // 结束时间
    
    // 结果数据
    void* result_data;              // 结果数据
    size_t result_size;             // 结果大小
    int error_code;                 // 错误码
    char error_message[256];        // 错误信息
    
    // 同步原语
    pthread_mutex_t mutex;          // 互斥锁
    pthread_cond_t cond;            // 条件变量
    
    // 链表节点
    struct task_internal* next;
} task_internal_t;

// 无锁环形队列
typedef struct {
    atomic_uint head;               // 头指针
    atomic_uint tail;               // 尾指针
    uint32_t capacity;              // 容量(必须是2的幂)
    uint32_t mask;                  // 掩码(capacity - 1)
    task_internal_t** tasks;        // 任务指针数组
    char padding[64];               // 缓存行对齐
} lockfree_queue_t;

// 共享内存区域
typedef struct {
    // 队列元数据
    atomic_uint producer_pos;       // 生产者位置
    atomic_uint consumer_pos;       // 消费者位置
    uint32_t queue_size;            // 队列大小
    
    // 统计信息
    atomic_ulong total_submitted;   // 总提交数
    atomic_ulong total_completed;   // 总完成数
    atomic_ulong total_failed;      // 总失败数
    
    // 任务数据区域
    char task_data[0];              // 变长任务数据
} shared_memory_t;

// Worker进程内部结构
typedef struct {
    uint32_t worker_id;             // Worker ID
    pid_t pid;                      // 进程ID
    atomic_int state;               // 状态
    
    // 通信文件描述符
    int task_eventfd;               // 任务通知eventfd
    int result_eventfd;             // 结果通知eventfd
    int control_eventfd;            // 控制命令eventfd
    
    // 共享内存
    shared_memory_t* shared_mem;    // 共享内存指针
    size_t shared_mem_size;         // 共享内存大小
    char shm_name[SHM_NAME_MAX_LEN]; // 共享内存名称
    
    // 统计信息
    atomic_ulong tasks_processed;   // 已处理任务数
    atomic_ulong last_heartbeat;    // 最后心跳时间
    atomic_uint current_task_id;    // 当前任务ID
    
    // 性能指标
    double cpu_usage;               // CPU使用率
    size_t memory_usage;            // 内存使用量
    
    // 进程控制
    pthread_t monitor_thread;       // 监控线程
    bool monitor_running;           // 监控线程运行标志
} worker_internal_t;

// Future对象内部结构
struct task_future {
    uint64_t task_id;               // 任务ID
    task_internal_t* task;          // 任务对象指针
    process_pool_t* pool;           // 进程池指针
    atomic_int ref_count;           // 引用计数
};

// 进程池内部结构
struct process_pool {
    // 配置信息
    pool_config_t config;           // 配置
    char pool_name[64];             // 进程池名称
    
    // 状态管理
    atomic_int state;               // 进程池状态
    atomic_uint next_task_id;       // 下一个任务ID
    
    // Worker管理
    worker_internal_t* workers;     // Worker数组
    atomic_uint active_workers;     // 活跃Worker数量
    atomic_uint target_workers;     // 目标Worker数量
    
    // 任务队列
    lockfree_queue_t* task_queue;   // 任务队列
    pthread_mutex_t queue_mutex;    // 队列互斥锁(fallback)
    
    // 事件循环
    int epoll_fd;                   // epoll文件描述符
    int timer_fd;                   // 定时器文件描述符
    int signal_fd;                  // 信号文件描述符
    pthread_t event_thread;         // 事件处理线程
    bool event_loop_running;        // 事件循环运行标志
    
    // 任务管理
    task_internal_t* pending_tasks; // 待处理任务链表
    task_internal_t* completed_tasks; // 已完成任务链表
    pthread_mutex_t task_mutex;     // 任务链表互斥锁
    
    // 统计信息
    pool_stats_t stats;             // 统计信息
    pthread_mutex_t stats_mutex;    // 统计信息互斥锁
    
    // 内存管理
    void* memory_pool;              // 内存池
    size_t memory_pool_size;        // 内存池大小
    
    // 监控和调试
    bool metrics_enabled;           // 指标收集开关
    bool tracing_enabled;           // 追踪开关
    FILE* log_file;                 // 日志文件
    int log_level;                  // 日志级别
    
    // 同步原语
    pthread_mutex_t pool_mutex;     // 进程池全局互斥锁
    pthread_cond_t shutdown_cond;   // 关闭条件变量
};

// ============================================================================
// 内部函数声明
// ============================================================================

// 队列操作
lockfree_queue_t* queue_create(uint32_t capacity);
void queue_destroy(lockfree_queue_t* queue);
bool queue_enqueue(lockfree_queue_t* queue, task_internal_t* task);
task_internal_t* queue_dequeue(lockfree_queue_t* queue);
bool queue_is_empty(lockfree_queue_t* queue);
bool queue_is_full(lockfree_queue_t* queue);
uint32_t queue_size(lockfree_queue_t* queue);

// Worker管理
pool_error_t worker_create(process_pool_t* pool, uint32_t worker_id);
pool_error_t worker_start(worker_internal_t* worker);
pool_error_t worker_stop(worker_internal_t* worker, uint32_t timeout_ms);
void worker_destroy(worker_internal_t* worker);
bool worker_is_alive(worker_internal_t* worker);
pool_error_t worker_send_task(worker_internal_t* worker, task_internal_t* task);
pool_error_t worker_get_result(worker_internal_t* worker, task_internal_t* task);

// 任务管理
task_internal_t* task_create(const task_desc_t* desc, const void* input_data, size_t input_size);
void task_destroy(task_internal_t* task);
pool_error_t task_set_result(task_internal_t* task, const void* result_data, size_t result_size);
pool_error_t task_set_error(task_internal_t* task, int error_code, const char* error_message);

// 事件处理
pool_error_t event_loop_init(process_pool_t* pool);
void event_loop_cleanup(process_pool_t* pool);
void* event_loop_thread(void* arg);
pool_error_t event_add_worker(process_pool_t* pool, worker_internal_t* worker);
pool_error_t event_remove_worker(process_pool_t* pool, worker_internal_t* worker);

// 共享内存
shared_memory_t* shm_create(const char* name, size_t size);
shared_memory_t* shm_open_existing(const char* name, size_t size);
void shm_destroy(shared_memory_t* shm, const char* name, size_t size);

// 内存管理
void* memory_pool_alloc(process_pool_t* pool, size_t size);
void memory_pool_free(process_pool_t* pool, void* ptr);
pool_error_t memory_pool_init(process_pool_t* pool, size_t size);
void memory_pool_cleanup(process_pool_t* pool);

// 监控和统计
void stats_update(process_pool_t* pool);
void stats_task_submitted(process_pool_t* pool);
void stats_task_completed(process_pool_t* pool, uint64_t duration_ns);
void stats_task_failed(process_pool_t* pool);

// 日志记录
void log_message(process_pool_t* pool, int level, const char* format, ...);

// 工具函数
uint64_t get_time_ns(void);
uint32_t next_power_of_2(uint32_t n);
bool is_power_of_2(uint32_t n);
int create_eventfd(void);
int create_timerfd(void);
int create_signalfd(void);

// 调试和追踪
void trace_task_start(task_internal_t* task);
void trace_task_end(task_internal_t* task);
void dump_pool_state(process_pool_t* pool);
void dump_worker_state(worker_internal_t* worker);

#endif // INTERNAL_H