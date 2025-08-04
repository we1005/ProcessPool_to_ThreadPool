#include "../../include/internal.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

// ============================================================================
// 任务ID生成器
// ============================================================================

static _Atomic uint64_t g_next_task_id = 1;

static uint64_t generate_task_id(void) {
    return ATOMIC_ADD(&g_next_task_id, 1);
}

// ============================================================================
// 任务内存池管理
// ============================================================================

// 任务内存池节点
typedef struct task_pool_node {
    task_internal_t task;
    struct task_pool_node* next;
} task_pool_node_t;

// 任务内存池
typedef struct task_pool {
    task_pool_node_t* free_list;
    pthread_mutex_t mutex;
    size_t total_allocated;
    size_t free_count;
    size_t max_pool_size;
} task_pool_t;

static task_pool_t g_task_pool = {
    .free_list = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .total_allocated = 0,
    .free_count = 0,
    .max_pool_size = 1000
};

static task_internal_t* task_pool_alloc(void) {
    pthread_mutex_lock(&g_task_pool.mutex);
    
    task_pool_node_t* node = NULL;
    
    if (g_task_pool.free_list) {
        // 从空闲列表获取
        node = g_task_pool.free_list;
        g_task_pool.free_list = node->next;
        g_task_pool.free_count--;
    } else {
        // 分配新节点
        node = malloc(sizeof(task_pool_node_t));
        if (node) {
            g_task_pool.total_allocated++;
        }
    }
    
    pthread_mutex_unlock(&g_task_pool.mutex);
    
    if (!node) {
        return NULL;
    }
    
    // 初始化任务结构
    memset(&node->task, 0, sizeof(task_internal_t));
    node->next = NULL;
    
    return &node->task;
}

static void task_pool_free(task_internal_t* task) {
    if (!task) return;
    
    task_pool_node_t* node = (task_pool_node_t*)((char*)task - offsetof(task_pool_node_t, task));
    
    pthread_mutex_lock(&g_task_pool.mutex);
    
    if (g_task_pool.free_count < g_task_pool.max_pool_size) {
        // 返回到空闲列表
        node->next = g_task_pool.free_list;
        g_task_pool.free_list = node;
        g_task_pool.free_count++;
    } else {
        // 直接释放
        free(node);
        g_task_pool.total_allocated--;
    }
    
    pthread_mutex_unlock(&g_task_pool.mutex);
}

void task_pool_cleanup(void) {
    pthread_mutex_lock(&g_task_pool.mutex);
    
    task_pool_node_t* current = g_task_pool.free_list;
    while (current) {
        task_pool_node_t* next = current->next;
        free(current);
        current = next;
    }
    
    g_task_pool.free_list = NULL;
    g_task_pool.free_count = 0;
    g_task_pool.total_allocated = 0;
    
    pthread_mutex_unlock(&g_task_pool.mutex);
}

// ============================================================================
// 任务创建和销毁
// ============================================================================

task_internal_t* task_create(const task_desc_t* desc) {
    if (!desc || !desc->handler) {
        return NULL;
    }
    
    task_internal_t* task = task_pool_alloc();
    if (!task) {
        return NULL;
    }
    
    // 生成唯一任务ID
    task->task_id = generate_task_id();
    
    // 复制任务描述
    task->desc = *desc;
    
    // 初始化状态
    ATOMIC_STORE(&task->state, TASK_STATE_PENDING);
    ATOMIC_STORE(&task->worker_id, UINT32_MAX);
    
    // 初始化时间戳
    task->submit_time_ns = get_time_ns();
    task->start_time_ns = 0;
    task->end_time_ns = 0;
    
    // 初始化引用计数
    ATOMIC_STORE(&task->ref_count, 1);
    
    // 初始化结果
    task->result.error_code = 0;
    task->result.error_message = NULL;
    task->result.output_data = NULL;
    task->result.output_size = 0;
    
    // 复制输入数据
    if (desc->input_data && desc->input_size > 0) {
        task->input_data = malloc(desc->input_size);
        if (!task->input_data) {
            task_pool_free(task);
            return NULL;
        }
        memcpy(task->input_data, desc->input_data, desc->input_size);
        task->input_size = desc->input_size;
    } else {
        task->input_data = NULL;
        task->input_size = 0;
    }
    
    // 初始化同步原语
    if (pthread_mutex_init(&task->mutex, NULL) != 0) {
        if (task->input_data) {
            free(task->input_data);
        }
        task_pool_free(task);
        return NULL;
    }
    
    if (pthread_cond_init(&task->completion_cond, NULL) != 0) {
        pthread_mutex_destroy(&task->mutex);
        if (task->input_data) {
            free(task->input_data);
        }
        task_pool_free(task);
        return NULL;
    }
    
    return task;
}

void task_destroy(task_internal_t* task) {
    if (!task) return;
    
    // 减少引用计数
    if (ATOMIC_SUB(&task->ref_count, 1) > 1) {
        return; // 还有其他引用
    }
    
    // 清理输入数据
    if (task->input_data) {
        free(task->input_data);
        task->input_data = NULL;
    }
    
    // 清理输出数据
    if (task->result.output_data) {
        free(task->result.output_data);
        task->result.output_data = NULL;
    }
    
    // 清理错误消息
    if (task->result.error_message) {
        free(task->result.error_message);
        task->result.error_message = NULL;
    }
    
    // 销毁同步原语
    pthread_mutex_destroy(&task->mutex);
    pthread_cond_destroy(&task->completion_cond);
    
    // 返回到内存池
    task_pool_free(task);
}

void task_ref(task_internal_t* task) {
    if (task) {
        ATOMIC_ADD(&task->ref_count, 1);
    }
}

void task_unref(task_internal_t* task) {
    if (task) {
        task_destroy(task);
    }
}

// ============================================================================
// 任务状态管理
// ============================================================================

bool task_is_completed(task_internal_t* task) {
    if (!task) return false;
    
    task_state_t state = ATOMIC_LOAD(&task->state);
    return (state == TASK_STATE_COMPLETED || 
            state == TASK_STATE_FAILED || 
            state == TASK_STATE_CANCELLED);
}

bool task_is_running(task_internal_t* task) {
    if (!task) return false;
    
    return ATOMIC_LOAD(&task->state) == TASK_STATE_RUNNING;
}

pool_error_t task_cancel(task_internal_t* task) {
    if (!task) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&task->mutex);
    
    task_state_t current_state = ATOMIC_LOAD(&task->state);
    
    if (current_state == TASK_STATE_PENDING) {
        // 任务还未开始，可以直接取消
        ATOMIC_STORE(&task->state, TASK_STATE_CANCELLED);
        task->end_time_ns = get_time_ns();
        
        // 通知等待的线程
        pthread_cond_broadcast(&task->completion_cond);
        
        pthread_mutex_unlock(&task->mutex);
        return POOL_SUCCESS;
    } else if (current_state == TASK_STATE_RUNNING) {
        // 任务正在运行，标记为取消请求
        // 实际的取消需要Worker进程配合
        ATOMIC_STORE(&task->state, TASK_STATE_CANCELLED);
        task->end_time_ns = get_time_ns();
        
        // 通知等待的线程
        pthread_cond_broadcast(&task->completion_cond);
        
        pthread_mutex_unlock(&task->mutex);
        return POOL_SUCCESS;
    } else {
        // 任务已完成，无法取消
        pthread_mutex_unlock(&task->mutex);
        return POOL_ERROR_INVALID_PARAM;
    }
}

pool_error_t task_wait(task_internal_t* task, uint32_t timeout_ms) {
    if (!task) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&task->mutex);
    
    // 如果任务已完成，直接返回
    if (task_is_completed(task)) {
        pthread_mutex_unlock(&task->mutex);
        return POOL_SUCCESS;
    }
    
    pool_error_t result = POOL_SUCCESS;
    
    if (timeout_ms == 0) {
        // 无限等待
        while (!task_is_completed(task)) {
            pthread_cond_wait(&task->completion_cond, &task->mutex);
        }
    } else {
        // 超时等待
        struct timespec abs_timeout;
        clock_gettime(CLOCK_REALTIME, &abs_timeout);
        
        abs_timeout.tv_sec += timeout_ms / 1000;
        abs_timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (abs_timeout.tv_nsec >= 1000000000) {
            abs_timeout.tv_sec++;
            abs_timeout.tv_nsec -= 1000000000;
        }
        
        while (!task_is_completed(task)) {
            int ret = pthread_cond_timedwait(&task->completion_cond, 
                                           &task->mutex, &abs_timeout);
            if (ret == ETIMEDOUT) {
                result = POOL_ERROR_TIMEOUT;
                break;
            } else if (ret != 0) {
                result = POOL_ERROR_SYSTEM_CALL;
                break;
            }
        }
    }
    
    pthread_mutex_unlock(&task->mutex);
    
    return result;
}

// ============================================================================
// 任务结果管理
// ============================================================================

pool_error_t task_set_result(task_internal_t* task, 
                            const void* output_data, 
                            size_t output_size) {
    if (!task) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&task->mutex);
    
    // 清理之前的结果
    if (task->result.output_data) {
        free(task->result.output_data);
        task->result.output_data = NULL;
        task->result.output_size = 0;
    }
    
    // 设置新结果
    if (output_data && output_size > 0) {
        task->result.output_data = malloc(output_size);
        if (!task->result.output_data) {
            pthread_mutex_unlock(&task->mutex);
            return POOL_ERROR_OUT_OF_MEMORY;
        }
        
        memcpy(task->result.output_data, output_data, output_size);
        task->result.output_size = output_size;
    }
    
    task->result.error_code = 0;
    
    pthread_mutex_unlock(&task->mutex);
    
    return POOL_SUCCESS;
}

pool_error_t task_set_error(task_internal_t* task, 
                           int error_code, 
                           const char* error_message) {
    if (!task) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&task->mutex);
    
    // 清理之前的错误消息
    if (task->result.error_message) {
        free(task->result.error_message);
        task->result.error_message = NULL;
    }
    
    // 设置错误信息
    task->result.error_code = error_code;
    
    if (error_message) {
        size_t msg_len = strlen(error_message) + 1;
        task->result.error_message = malloc(msg_len);
        if (task->result.error_message) {
            strcpy(task->result.error_message, error_message);
        }
    }
    
    pthread_mutex_unlock(&task->mutex);
    
    return POOL_SUCCESS;
}

pool_error_t task_get_result(task_internal_t* task, task_result_t* result) {
    if (!task || !result) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    if (!task_is_completed(task)) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&task->mutex);
    
    // 复制结果
    result->error_code = task->result.error_code;
    
    // 复制输出数据
    if (task->result.output_data && task->result.output_size > 0) {
        result->output_data = malloc(task->result.output_size);
        if (!result->output_data) {
            pthread_mutex_unlock(&task->mutex);
            return POOL_ERROR_OUT_OF_MEMORY;
        }
        
        memcpy(result->output_data, task->result.output_data, task->result.output_size);
        result->output_size = task->result.output_size;
    } else {
        result->output_data = NULL;
        result->output_size = 0;
    }
    
    // 复制错误消息
    if (task->result.error_message) {
        size_t msg_len = strlen(task->result.error_message) + 1;
        result->error_message = malloc(msg_len);
        if (result->error_message) {
            strcpy(result->error_message, task->result.error_message);
        }
    } else {
        result->error_message = NULL;
    }
    
    pthread_mutex_unlock(&task->mutex);
    
    return POOL_SUCCESS;
}

void task_result_cleanup(task_result_t* result) {
    if (!result) return;
    
    if (result->output_data) {
        free(result->output_data);
        result->output_data = NULL;
    }
    
    if (result->error_message) {
        free(result->error_message);
        result->error_message = NULL;
    }
    
    result->output_size = 0;
    result->error_code = 0;
}

// ============================================================================
// Future对象管理
// ============================================================================

task_future_t* future_create(task_internal_t* task) {
    if (!task) {
        return NULL;
    }
    
    task_future_t* future = malloc(sizeof(task_future_t));
    if (!future) {
        return NULL;
    }
    
    future->task = task;
    task_ref(task); // 增加任务引用计数
    
    ATOMIC_STORE(&future->ref_count, 1);
    
    if (pthread_mutex_init(&future->mutex, NULL) != 0) {
        task_unref(task);
        free(future);
        return NULL;
    }
    
    return future;
}

void future_destroy(task_future_t* future) {
    if (!future) return;
    
    // 减少引用计数
    if (ATOMIC_SUB(&future->ref_count, 1) > 1) {
        return; // 还有其他引用
    }
    
    pthread_mutex_destroy(&future->mutex);
    
    if (future->task) {
        task_unref(future->task);
    }
    
    free(future);
}

void future_ref(task_future_t* future) {
    if (future) {
        ATOMIC_ADD(&future->ref_count, 1);
    }
}

void future_unref(task_future_t* future) {
    if (future) {
        future_destroy(future);
    }
}

pool_error_t future_wait(task_future_t* future, uint32_t timeout_ms) {
    if (!future || !future->task) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    return task_wait(future->task, timeout_ms);
}

pool_error_t future_cancel(task_future_t* future) {
    if (!future || !future->task) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    return task_cancel(future->task);
}

pool_error_t future_get_result(task_future_t* future, task_result_t* result) {
    if (!future || !future->task || !result) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    return task_get_result(future->task, result);
}

bool future_is_ready(task_future_t* future) {
    if (!future || !future->task) {
        return false;
    }
    
    return task_is_completed(future->task);
}

// ============================================================================
// 批量任务管理
// ============================================================================

pool_error_t task_batch_wait(task_future_t** futures, 
                            size_t count, 
                            uint32_t timeout_ms) {
    if (!futures || count == 0) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    uint64_t start_time = get_time_ns();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    
    for (size_t i = 0; i < count; i++) {
        if (!futures[i]) {
            continue;
        }
        
        uint32_t remaining_timeout = timeout_ms;
        if (timeout_ms > 0) {
            uint64_t elapsed_ns = get_time_ns() - start_time;
            if (elapsed_ns >= timeout_ns) {
                return POOL_ERROR_TIMEOUT;
            }
            remaining_timeout = (uint32_t)((timeout_ns - elapsed_ns) / 1000000ULL);
        }
        
        pool_error_t result = future_wait(futures[i], remaining_timeout);
        if (result != POOL_SUCCESS) {
            return result;
        }
    }
    
    return POOL_SUCCESS;
}

pool_error_t task_batch_cancel(task_future_t** futures, size_t count) {
    if (!futures || count == 0) {
        return POOL_ERROR_INVALID_PARAM;
    }
    
    pool_error_t last_error = POOL_SUCCESS;
    
    for (size_t i = 0; i < count; i++) {
        if (futures[i]) {
            pool_error_t result = future_cancel(futures[i]);
            if (result != POOL_SUCCESS) {
                last_error = result;
            }
        }
    }
    
    return last_error;
}

// ============================================================================
// 任务统计和监控
// ============================================================================

void task_get_timing_info(task_internal_t* task, 
                         uint64_t* submit_time_ns,
                         uint64_t* start_time_ns,
                         uint64_t* end_time_ns) {
    if (!task) return;
    
    if (submit_time_ns) {
        *submit_time_ns = task->submit_time_ns;
    }
    
    if (start_time_ns) {
        *start_time_ns = task->start_time_ns;
    }
    
    if (end_time_ns) {
        *end_time_ns = task->end_time_ns;
    }
}

uint64_t task_get_queue_time_ns(task_internal_t* task) {
    if (!task || task->start_time_ns == 0) {
        return 0;
    }
    
    return task->start_time_ns - task->submit_time_ns;
}

uint64_t task_get_execution_time_ns(task_internal_t* task) {
    if (!task || task->start_time_ns == 0 || task->end_time_ns == 0) {
        return 0;
    }
    
    return task->end_time_ns - task->start_time_ns;
}

uint64_t task_get_total_time_ns(task_internal_t* task) {
    if (!task || task->end_time_ns == 0) {
        return 0;
    }
    
    return task->end_time_ns - task->submit_time_ns;
}