#include "../../include/process_pool.h"
#include "../../include/internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>

// 全局变量
static atomic_uint g_next_pool_id = 1;
static int g_log_level = 2; // INFO级别

// 进程池状态枚举
enum pool_state {
    POOL_STATE_CREATED = 0,
    POOL_STATE_STARTING = 1,
    POOL_STATE_RUNNING = 2,
    POOL_STATE_STOPPING = 3,
    POOL_STATE_STOPPED = 4
};

// ============================================================================
// 内部辅助函数
// ============================================================================

static void init_default_config(pool_config_t* config) {
    memset(config, 0, sizeof(pool_config_t));
    config->min_workers = 2;
    config->max_workers = 8;
    config->queue_size = DEFAULT_QUEUE_SIZE;
    config->worker_idle_timeout = 300; // 5分钟
    config->task_timeout = 30; // 30秒
    config->enable_auto_scaling = true;
    config->enable_metrics = true;
    config->enable_tracing = false;
    config->pool_name = "default_pool";
    config->default_handler = NULL;
    config->user_context = NULL;
}

static bool validate_config(const pool_config_t* config) {
    if (!config) return false;
    
    if (config->min_workers == 0 || config->min_workers > MAX_WORKERS) {
        log_message(NULL, 0, "Invalid min_workers: %u", config->min_workers);
        return false;
    }
    
    if (config->max_workers < config->min_workers || config->max_workers > MAX_WORKERS) {
        log_message(NULL, 0, "Invalid max_workers: %u", config->max_workers);
        return false;
    }
    
    if (config->queue_size == 0 || !is_power_of_2(config->queue_size)) {
        log_message(NULL, 0, "Queue size must be power of 2: %u", config->queue_size);
        return false;
    }
    
    return true;
}

static pool_error_t init_pool_resources(process_pool_t* pool) {
    // 初始化互斥锁和条件变量
    if (pthread_mutex_init(&pool->pool_mutex, NULL) != 0) {
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0) {
        pthread_mutex_destroy(&pool->pool_mutex);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    if (pthread_mutex_init(&pool->task_mutex, NULL) != 0) {
        pthread_mutex_destroy(&pool->queue_mutex);
        pthread_mutex_destroy(&pool->pool_mutex);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    if (pthread_mutex_init(&pool->stats_mutex, NULL) != 0) {
        pthread_mutex_destroy(&pool->task_mutex);
        pthread_mutex_destroy(&pool->queue_mutex);
        pthread_mutex_destroy(&pool->pool_mutex);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    if (pthread_cond_init(&pool->shutdown_cond, NULL) != 0) {
        pthread_mutex_destroy(&pool->stats_mutex);
        pthread_mutex_destroy(&pool->task_mutex);
        pthread_mutex_destroy(&pool->queue_mutex);
        pthread_mutex_destroy(&pool->pool_mutex);
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    // 创建任务队列
    pool->task_queue = queue_create(pool->config.queue_size);
    if (!pool->task_queue) {
        pthread_cond_destroy(&pool->shutdown_cond);
        pthread_mutex_destroy(&pool->stats_mutex);
        pthread_mutex_destroy(&pool->task_mutex);
        pthread_mutex_destroy(&pool->queue_mutex);
        pthread_mutex_destroy(&pool->pool_mutex);
        return POOL_ERROR_NO_MEMORY;
    }
    
    // 分配Worker数组
    pool->workers = calloc(pool->config.max_workers, sizeof(worker_internal_t));
    if (!pool->workers) {
        queue_destroy(pool->task_queue);
        pthread_cond_destroy(&pool->shutdown_cond);
        pthread_mutex_destroy(&pool->stats_mutex);
        pthread_mutex_destroy(&pool->task_mutex);
        pthread_mutex_destroy(&pool->queue_mutex);
        pthread_mutex_destroy(&pool->pool_mutex);
        return POOL_ERROR_NO_MEMORY;
    }
    
    // 初始化内存池
    pool_error_t err = memory_pool_init(pool, 1024 * 1024); // 1MB
    if (err != POOL_SUCCESS) {
        free(pool->workers);
        queue_destroy(pool->task_queue);
        pthread_cond_destroy(&pool->shutdown_cond);
        pthread_mutex_destroy(&pool->stats_mutex);
        pthread_mutex_destroy(&pool->task_mutex);
        pthread_mutex_destroy(&pool->queue_mutex);
        pthread_mutex_destroy(&pool->pool_mutex);
        return err;
    }
    
    // 初始化事件循环
    err = event_loop_init(pool);
    if (err != POOL_SUCCESS) {
        memory_pool_cleanup(pool);
        free(pool->workers);
        queue_destroy(pool->task_queue);
        pthread_cond_destroy(&pool->shutdown_cond);
        pthread_mutex_destroy(&pool->stats_mutex);
        pthread_mutex_destroy(&pool->task_mutex);
        pthread_mutex_destroy(&pool->queue_mutex);
        pthread_mutex_destroy(&pool->pool_mutex);
        return err;
    }
    
    return POOL_SUCCESS;
}

static void cleanup_pool_resources(process_pool_t* pool) {
    if (!pool) return;
    
    // 清理事件循环
    event_loop_cleanup(pool);
    
    // 清理内存池
    memory_pool_cleanup(pool);
    
    // 释放Worker数组
    if (pool->workers) {
        free(pool->workers);
        pool->workers = NULL;
    }
    
    // 销毁任务队列
    if (pool->task_queue) {
        queue_destroy(pool->task_queue);
        pool->task_queue = NULL;
    }
    
    // 销毁同步原语
    pthread_cond_destroy(&pool->shutdown_cond);
    pthread_mutex_destroy(&pool->stats_mutex);
    pthread_mutex_destroy(&pool->task_mutex);
    pthread_mutex_destroy(&pool->queue_mutex);
    pthread_mutex_destroy(&pool->pool_mutex);
    
    // 关闭日志文件
    if (pool->log_file && pool->log_file != stdout && pool->log_file != stderr) {
        fclose(pool->log_file);
        pool->log_file = NULL;
    }
}

// ============================================================================
// 公共API实现
// ============================================================================

process_pool_t* pool_create(const pool_config_t* config) {
    pool_config_t default_config;
    
    // 使用默认配置或验证用户配置
    if (!config) {
        init_default_config(&default_config);
        config = &default_config;
    } else if (!validate_config(config)) {
        return NULL;
    }
    
    // 分配进程池结构
    process_pool_t* pool = calloc(1, sizeof(process_pool_t));
    if (!pool) {
        log_message(NULL, 0, "Failed to allocate process pool");
        return NULL;
    }
    
    // 复制配置
    pool->config = *config;
    if (config->pool_name) {
        strncpy(pool->pool_name, config->pool_name, sizeof(pool->pool_name) - 1);
        pool->config.pool_name = pool->pool_name;
    }
    
    // 初始化状态
    ATOMIC_STORE(&pool->state, POOL_STATE_CREATED);
    ATOMIC_STORE(&pool->next_task_id, 1);
    ATOMIC_STORE(&pool->active_workers, 0);
    ATOMIC_STORE(&pool->target_workers, config->min_workers);
    
    // 初始化统计信息
    memset(&pool->stats, 0, sizeof(pool_stats_t));
    pool->stats.uptime_seconds = get_time_ns() / 1000000000ULL;
    
    // 设置日志
    pool->log_level = g_log_level;
    pool->log_file = stdout;
    pool->metrics_enabled = config->enable_metrics;
    pool->tracing_enabled = config->enable_tracing;
    
    // 初始化资源
    pool_error_t err = init_pool_resources(pool);
    if (err != POOL_SUCCESS) {
        log_message(pool, 0, "Failed to initialize pool resources: %s", 
                   pool_error_string(err));
        free(pool);
        return NULL;
    }
    
    log_message(pool, 2, "Process pool '%s' created successfully", 
               pool->config.pool_name);
    
    return pool;
}

pool_error_t pool_start(process_pool_t* pool) {
    if (!pool) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&pool->pool_mutex);
    
    int current_state = ATOMIC_LOAD(&pool->state);
    if (current_state != POOL_STATE_CREATED) {
        pthread_mutex_unlock(&pool->pool_mutex);
        log_message(pool, 1, "Pool is not in created state: %d", current_state);
        return POOL_ERROR_INVALID_PARAM;
    }
    
    ATOMIC_STORE(&pool->state, POOL_STATE_STARTING);
    
    log_message(pool, 2, "Starting process pool with %u workers", 
               pool->config.min_workers);
    
    // 启动事件循环线程
    pool->event_loop_running = true;
    if (pthread_create(&pool->event_thread, NULL, event_loop_thread, pool) != 0) {
        ATOMIC_STORE(&pool->state, POOL_STATE_CREATED);
        pthread_mutex_unlock(&pool->pool_mutex);
        log_message(pool, 0, "Failed to create event loop thread");
        return POOL_ERROR_SYSTEM_CALL;
    }
    
    // 创建初始Worker进程
    pool_error_t err = POOL_SUCCESS;
    for (uint32_t i = 0; i < pool->config.min_workers; i++) {
        err = worker_create(pool, i);
        if (err != POOL_SUCCESS) {
            log_message(pool, 0, "Failed to create worker %u: %s", 
                       i, pool_error_string(err));
            break;
        }
        
        err = worker_start(&pool->workers[i]);
        if (err != POOL_SUCCESS) {
            log_message(pool, 0, "Failed to start worker %u: %s", 
                       i, pool_error_string(err));
            worker_destroy(&pool->workers[i]);
            break;
        }
        
        ATOMIC_ADD(&pool->active_workers, 1);
        log_message(pool, 3, "Worker %u started successfully", i);
    }
    
    if (err == POOL_SUCCESS) {
        ATOMIC_STORE(&pool->state, POOL_STATE_RUNNING);
        log_message(pool, 2, "Process pool started successfully with %u workers", 
                   ATOMIC_LOAD(&pool->active_workers));
    } else {
        // 启动失败，清理已创建的Worker
        ATOMIC_STORE(&pool->state, POOL_STATE_STOPPING);
        for (uint32_t i = 0; i < pool->config.max_workers; i++) {
            if (pool->workers[i].pid > 0) {
                worker_stop(&pool->workers[i], 5000); // 5秒超时
                worker_destroy(&pool->workers[i]);
            }
        }
        ATOMIC_STORE(&pool->active_workers, 0);
        ATOMIC_STORE(&pool->state, POOL_STATE_CREATED);
    }
    
    pthread_mutex_unlock(&pool->pool_mutex);
    return err;
}

pool_error_t pool_stop(process_pool_t* pool, uint32_t timeout_ms) {
    if (!pool) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&pool->pool_mutex);
    
    int current_state = ATOMIC_LOAD(&pool->state);
    if (current_state != POOL_STATE_RUNNING) {
        pthread_mutex_unlock(&pool->pool_mutex);
        log_message(pool, 1, "Pool is not running: %d", current_state);
        return POOL_ERROR_INVALID_PARAM;
    }
    
    ATOMIC_STORE(&pool->state, POOL_STATE_STOPPING);
    
    log_message(pool, 2, "Stopping process pool...");
    
    // 停止接受新任务
    pool->event_loop_running = false;
    
    // 等待所有正在运行的任务完成
    uint64_t start_time = get_time_ns();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    
    while (pool->stats.running_tasks > 0) {
        uint64_t elapsed = get_time_ns() - start_time;
        if (elapsed >= timeout_ns) {
            log_message(pool, 1, "Timeout waiting for tasks to complete");
            break;
        }
        
        pthread_mutex_unlock(&pool->pool_mutex);
        usleep(10000); // 10ms
        pthread_mutex_lock(&pool->pool_mutex);
    }
    
    // 停止所有Worker进程
    uint32_t active_workers = ATOMIC_LOAD(&pool->active_workers);
    for (uint32_t i = 0; i < pool->config.max_workers; i++) {
        if (pool->workers[i].pid > 0) {
            worker_stop(&pool->workers[i], timeout_ms / active_workers);
            worker_destroy(&pool->workers[i]);
        }
    }
    
    ATOMIC_STORE(&pool->active_workers, 0);
    
    // 停止事件循环线程
    if (pool->event_thread) {
        pthread_join(pool->event_thread, NULL);
        pool->event_thread = 0;
    }
    
    ATOMIC_STORE(&pool->state, POOL_STATE_STOPPED);
    
    log_message(pool, 2, "Process pool stopped successfully");
    
    pthread_cond_broadcast(&pool->shutdown_cond);
    pthread_mutex_unlock(&pool->pool_mutex);
    
    return POOL_SUCCESS;
}

void pool_destroy(process_pool_t* pool) {
    if (!pool) return;
    
    log_message(pool, 2, "Destroying process pool '%s'", pool->config.pool_name);
    
    // 确保进程池已停止
    int current_state = ATOMIC_LOAD(&pool->state);
    if (current_state == POOL_STATE_RUNNING) {
        pool_stop(pool, 10000); // 10秒超时
    }
    
    // 清理资源
    cleanup_pool_resources(pool);
    
    // 释放进程池结构
    free(pool);
    
    log_message(NULL, 2, "Process pool destroyed");
}

pool_error_t pool_get_stats(process_pool_t* pool, pool_stats_t* stats) {
    if (!pool || !stats) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&pool->stats_mutex);
    
    // 更新实时统计信息
    stats_update(pool);
    
    // 复制统计信息
    *stats = pool->stats;
    
    pthread_mutex_unlock(&pool->stats_mutex);
    
    return POOL_SUCCESS;
}

pool_error_t pool_resize(process_pool_t* pool, uint32_t target_count) {
    if (!pool) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    if (target_count < pool->config.min_workers || 
        target_count > pool->config.max_workers) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&pool->pool_mutex);
    
    uint32_t current_count = ATOMIC_LOAD(&pool->active_workers);
    ATOMIC_STORE(&pool->target_workers, target_count);
    
    log_message(pool, 2, "Resizing pool from %u to %u workers", 
               current_count, target_count);
    
    pool_error_t err = POOL_SUCCESS;
    
    if (target_count > current_count) {
        // 增加Worker
        for (uint32_t i = current_count; i < target_count; i++) {
            err = worker_create(pool, i);
            if (err != POOL_SUCCESS) break;
            
            err = worker_start(&pool->workers[i]);
            if (err != POOL_SUCCESS) {
                worker_destroy(&pool->workers[i]);
                break;
            }
            
            ATOMIC_ADD(&pool->active_workers, 1);
        }
    } else if (target_count < current_count) {
        // 减少Worker
        for (uint32_t i = target_count; i < current_count; i++) {
            if (pool->workers[i].pid > 0) {
                worker_stop(&pool->workers[i], 5000);
                worker_destroy(&pool->workers[i]);
                ATOMIC_SUB(&pool->active_workers, 1);
            }
        }
    }
    
    pthread_mutex_unlock(&pool->pool_mutex);
    
    log_message(pool, 2, "Pool resized to %u workers", 
               ATOMIC_LOAD(&pool->active_workers));
    
    return err;
}

// ============================================================================
// 工具函数实现
// ============================================================================

const char* pool_error_string(pool_error_t error) {
    switch (error) {
        case POOL_SUCCESS: return "Success";
        case POOL_ERROR_INVALID_PARAM: return "Invalid parameter";
        case POOL_ERROR_NO_MEMORY: return "Out of memory";
        case POOL_ERROR_SYSTEM_CALL: return "System call failed";
        case POOL_ERROR_TIMEOUT: return "Operation timeout";
        case POOL_ERROR_QUEUE_FULL: return "Task queue full";
        case POOL_ERROR_WORKER_DEAD: return "Worker process died";
        case POOL_ERROR_SHUTDOWN: return "Pool is shutting down";
        default: return "Unknown error";
    }
}

uint64_t pool_get_time_ns(void) {
    return get_time_ns();
}

void pool_set_log_level(int level) {
    g_log_level = level;
}

const char* pool_get_version(void) {
    static char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d",
             PROCESS_POOL_VERSION_MAJOR,
             PROCESS_POOL_VERSION_MINOR,
             PROCESS_POOL_VERSION_PATCH);
    return version;
}