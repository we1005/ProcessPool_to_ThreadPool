#ifndef PROCESS_POOL_H
#define PROCESS_POOL_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// 现代进程池版本信息
#define PROCESS_POOL_VERSION_MAJOR 2
#define PROCESS_POOL_VERSION_MINOR 0
#define PROCESS_POOL_VERSION_PATCH 0

// 配置常量
#define MAX_WORKERS 128
#define MAX_TASK_DATA_SIZE (64 * 1024)  // 64KB
#define MAX_RESULT_DATA_SIZE (64 * 1024)
#define DEFAULT_QUEUE_SIZE 4096
#define MAX_TASK_NAME_LEN 64

// 错误码定义
typedef enum {
    POOL_SUCCESS = 0,
    POOL_ERROR_INVALID_PARAM = -1,
    POOL_ERROR_NO_MEMORY = -2,
    POOL_ERROR_SYSTEM_CALL = -3,
    POOL_ERROR_TIMEOUT = -4,
    POOL_ERROR_QUEUE_FULL = -5,
    POOL_ERROR_WORKER_DEAD = -6,
    POOL_ERROR_SHUTDOWN = -7
} pool_error_t;

// 任务优先级
typedef enum {
    TASK_PRIORITY_LOW = 0,
    TASK_PRIORITY_NORMAL = 1,
    TASK_PRIORITY_HIGH = 2,
    TASK_PRIORITY_URGENT = 3
} task_priority_t;

// Worker状态
typedef enum {
    WORKER_STATE_IDLE = 0,
    WORKER_STATE_BUSY = 1,
    WORKER_STATE_STARTING = 2,
    WORKER_STATE_STOPPING = 3,
    WORKER_STATE_DEAD = 4
} worker_state_t;

// 任务状态
typedef enum {
    TASK_STATE_PENDING = 0,
    TASK_STATE_RUNNING = 1,
    TASK_STATE_COMPLETED = 2,
    TASK_STATE_FAILED = 3,
    TASK_STATE_TIMEOUT = 4,
    TASK_STATE_CANCELLED = 5
} task_state_t;

// 前向声明
typedef struct process_pool process_pool_t;
typedef struct task_future task_future_t;

// 任务处理函数类型
typedef int (*task_handler_t)(const void* input_data, size_t input_size,
                             void** output_data, size_t* output_size,
                             void* user_context);

// 任务完成回调函数类型
typedef void (*task_callback_t)(uint64_t task_id, task_state_t state,
                               const void* result_data, size_t result_size,
                               void* user_data);

// 进程池配置结构
typedef struct {
    uint32_t min_workers;           // 最小worker数量
    uint32_t max_workers;           // 最大worker数量
    uint32_t queue_size;            // 任务队列大小
    uint32_t worker_idle_timeout;   // worker空闲超时(秒)
    uint32_t task_timeout;          // 任务超时(秒)
    bool enable_auto_scaling;       // 是否启用自动扩缩容
    bool enable_metrics;            // 是否启用指标收集
    bool enable_tracing;            // 是否启用分布式追踪
    const char* pool_name;          // 进程池名称
    task_handler_t default_handler; // 默认任务处理函数
    void* user_context;             // 用户上下文
} pool_config_t;

// 任务描述结构
typedef struct {
    char name[MAX_TASK_NAME_LEN];   // 任务名称
    task_priority_t priority;       // 任务优先级
    uint32_t timeout_ms;            // 任务超时(毫秒)
    task_handler_t handler;         // 自定义处理函数(可选)
    task_callback_t callback;       // 完成回调(可选)
    void* callback_data;            // 回调用户数据
    uint64_t trace_id;              // 追踪ID
} task_desc_t;

// 任务结果结构
typedef struct {
    uint64_t task_id;               // 任务ID
    task_state_t state;             // 任务状态
    int error_code;                 // 错误码
    char error_message[256];        // 错误信息
    void* result_data;              // 结果数据
    size_t result_size;             // 结果大小
    uint64_t start_time_ns;         // 开始时间(纳秒)
    uint64_t end_time_ns;           // 结束时间(纳秒)
    uint32_t worker_id;             // 处理的worker ID
} task_result_t;

// 进程池统计信息
typedef struct {
    uint32_t active_workers;        // 活跃worker数量
    uint32_t idle_workers;          // 空闲worker数量
    uint32_t pending_tasks;         // 待处理任务数
    uint32_t running_tasks;         // 正在运行的任务数
    uint64_t total_submitted;       // 总提交任务数
    uint64_t total_completed;       // 总完成任务数
    uint64_t total_failed;          // 总失败任务数
    uint64_t avg_task_time_ns;      // 平均任务处理时间
    uint64_t max_task_time_ns;      // 最大任务处理时间
    double cpu_usage;               // CPU使用率
    size_t memory_usage;            // 内存使用量
    uint64_t uptime_seconds;        // 运行时间
} pool_stats_t;

// Worker信息结构
typedef struct {
    uint32_t worker_id;             // Worker ID
    pid_t pid;                      // 进程ID
    worker_state_t state;           // 状态
    uint64_t tasks_processed;       // 已处理任务数
    uint64_t last_activity_time;    // 最后活动时间
    double cpu_usage;               // CPU使用率
    size_t memory_usage;            // 内存使用量
    uint64_t current_task_id;       // 当前处理的任务ID
} worker_info_t;

// ============================================================================
// 核心API函数
// ============================================================================

/**
 * 创建进程池
 * @param config 配置参数
 * @return 进程池句柄，失败返回NULL
 */
process_pool_t* pool_create(const pool_config_t* config);

/**
 * 启动进程池
 * @param pool 进程池句柄
 * @return 成功返回POOL_SUCCESS
 */
pool_error_t pool_start(process_pool_t* pool);

/**
 * 提交任务(同步)
 * @param pool 进程池句柄
 * @param desc 任务描述
 * @param input_data 输入数据
 * @param input_size 输入数据大小
 * @param result 结果输出
 * @param timeout_ms 超时时间(毫秒)
 * @return 成功返回POOL_SUCCESS
 */
pool_error_t pool_submit_sync(process_pool_t* pool,
                             const task_desc_t* desc,
                             const void* input_data,
                             size_t input_size,
                             task_result_t* result,
                             uint32_t timeout_ms);

/**
 * 提交任务(异步)
 * @param pool 进程池句柄
 * @param desc 任务描述
 * @param input_data 输入数据
 * @param input_size 输入数据大小
 * @param future 返回的future对象
 * @return 成功返回POOL_SUCCESS
 */
pool_error_t pool_submit_async(process_pool_t* pool,
                              const task_desc_t* desc,
                              const void* input_data,
                              size_t input_size,
                              task_future_t** future);

/**
 * 批量提交任务
 * @param pool 进程池句柄
 * @param tasks 任务数组
 * @param count 任务数量
 * @param futures 返回的future数组
 * @return 成功返回POOL_SUCCESS
 */
pool_error_t pool_submit_batch(process_pool_t* pool,
                              const task_desc_t* tasks,
                              const void** input_data,
                              const size_t* input_sizes,
                              uint32_t count,
                              task_future_t** futures);

/**
 * 等待任务完成
 * @param future future对象
 * @param result 结果输出
 * @param timeout_ms 超时时间(毫秒)
 * @return 成功返回POOL_SUCCESS
 */
pool_error_t pool_future_wait(task_future_t* future,
                             task_result_t* result,
                             uint32_t timeout_ms);

/**
 * 取消任务
 * @param future future对象
 * @return 成功返回POOL_SUCCESS
 */
pool_error_t pool_future_cancel(task_future_t* future);

/**
 * 释放future对象
 * @param future future对象
 */
void pool_future_destroy(task_future_t* future);

/**
 * 获取进程池统计信息
 * @param pool 进程池句柄
 * @param stats 统计信息输出
 * @return 成功返回POOL_SUCCESS
 */
pool_error_t pool_get_stats(process_pool_t* pool, pool_stats_t* stats);

/**
 * 获取worker信息
 * @param pool 进程池句柄
 * @param workers worker信息数组
 * @param count 数组大小，返回实际数量
 * @return 成功返回POOL_SUCCESS
 */
pool_error_t pool_get_workers(process_pool_t* pool,
                             worker_info_t* workers,
                             uint32_t* count);

/**
 * 动态调整worker数量
 * @param pool 进程池句柄
 * @param target_count 目标worker数量
 * @return 成功返回POOL_SUCCESS
 */
pool_error_t pool_resize(process_pool_t* pool, uint32_t target_count);

/**
 * 优雅停止进程池
 * @param pool 进程池句柄
 * @param timeout_ms 超时时间(毫秒)
 * @return 成功返回POOL_SUCCESS
 */
pool_error_t pool_stop(process_pool_t* pool, uint32_t timeout_ms);

/**
 * 销毁进程池
 * @param pool 进程池句柄
 */
void pool_destroy(process_pool_t* pool);

// ============================================================================
// 工具函数
// ============================================================================

/**
 * 获取错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char* pool_error_string(pool_error_t error);

/**
 * 获取当前时间戳(纳秒)
 * @return 时间戳
 */
uint64_t pool_get_time_ns(void);

/**
 * 设置日志级别
 * @param level 日志级别(0-4)
 */
void pool_set_log_level(int level);

/**
 * 获取版本信息
 * @return 版本字符串
 */
const char* pool_get_version(void);

#ifdef __cplusplus
}
#endif

#endif // PROCESS_POOL_H