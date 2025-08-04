#include "../../include/internal.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ============================================================================
// 无锁环形队列实现
// ============================================================================

/**
 * 基于原子操作的无锁环形队列
 * 使用单生产者单消费者(SPSC)模型，适合Master-Worker架构
 * 采用内存屏障确保在多核系统上的正确性
 */

lockfree_queue_t* queue_create(uint32_t capacity) {
    // 容量必须是2的幂，便于使用位运算优化
    if (capacity == 0 || !is_power_of_2(capacity)) {
        capacity = next_power_of_2(capacity);
    }
    
    if (capacity > (1U << 20)) { // 限制最大1M个元素
        return NULL;
    }
    
    lockfree_queue_t* queue = aligned_alloc(64, sizeof(lockfree_queue_t));
    if (!queue) {
        return NULL;
    }
    
    // 分配任务指针数组，使用缓存行对齐
    queue->tasks = aligned_alloc(64, capacity * sizeof(task_internal_t*));
    if (!queue->tasks) {
        free(queue);
        return NULL;
    }
    
    // 初始化队列元数据
    ATOMIC_STORE(&queue->head, 0);
    ATOMIC_STORE(&queue->tail, 0);
    queue->capacity = capacity;
    queue->mask = capacity - 1;
    
    // 清零任务指针数组
    memset(queue->tasks, 0, capacity * sizeof(task_internal_t*));
    
    return queue;
}

void queue_destroy(lockfree_queue_t* queue) {
    if (!queue) return;
    
    // 清理剩余的任务
    task_internal_t* task;
    while ((task = queue_dequeue(queue)) != NULL) {
        task_destroy(task);
    }
    
    free(queue->tasks);
    free(queue);
}

bool queue_enqueue(lockfree_queue_t* queue, task_internal_t* task) {
    if (!queue || !task) {
        return false;
    }
    
    uint32_t head = ATOMIC_LOAD(&queue->head);
    uint32_t tail = ATOMIC_LOAD(&queue->tail);
    
    // 检查队列是否已满
    // 保留一个空位来区分满和空的状态
    if (((tail + 1) & queue->mask) == (head & queue->mask)) {
        return false; // 队列已满
    }
    
    // 写入任务指针
    queue->tasks[tail & queue->mask] = task;
    
    // 内存屏障确保写入完成后再更新tail
    MEMORY_BARRIER();
    
    // 更新tail指针
    ATOMIC_STORE(&queue->tail, tail + 1);
    
    return true;
}

task_internal_t* queue_dequeue(lockfree_queue_t* queue) {
    if (!queue) {
        return NULL;
    }
    
    uint32_t head = ATOMIC_LOAD(&queue->head);
    uint32_t tail = ATOMIC_LOAD(&queue->tail);
    
    // 检查队列是否为空
    if ((head & queue->mask) == (tail & queue->mask)) {
        return NULL; // 队列为空
    }
    
    // 读取任务指针
    task_internal_t* task = queue->tasks[head & queue->mask];
    
    // 清零指针位置
    queue->tasks[head & queue->mask] = NULL;
    
    // 内存屏障确保读取完成后再更新head
    MEMORY_BARRIER();
    
    // 更新head指针
    ATOMIC_STORE(&queue->head, head + 1);
    
    return task;
}

bool queue_is_empty(lockfree_queue_t* queue) {
    if (!queue) {
        return true;
    }
    
    uint32_t head = ATOMIC_LOAD(&queue->head);
    uint32_t tail = ATOMIC_LOAD(&queue->tail);
    
    return (head & queue->mask) == (tail & queue->mask);
}

bool queue_is_full(lockfree_queue_t* queue) {
    if (!queue) {
        return true;
    }
    
    uint32_t head = ATOMIC_LOAD(&queue->head);
    uint32_t tail = ATOMIC_LOAD(&queue->tail);
    
    return ((tail + 1) & queue->mask) == (head & queue->mask);
}

uint32_t queue_size(lockfree_queue_t* queue) {
    if (!queue) {
        return 0;
    }
    
    uint32_t head = ATOMIC_LOAD(&queue->head);
    uint32_t tail = ATOMIC_LOAD(&queue->tail);
    
    // 计算当前队列中的元素数量
    return (tail - head) & queue->mask;
}

// ============================================================================
// 批量操作接口(可选优化)
// ============================================================================

/**
 * 批量入队操作，减少原子操作开销
 */
bool queue_enqueue_batch(lockfree_queue_t* queue, 
                        task_internal_t** tasks, 
                        uint32_t count,
                        uint32_t* enqueued) {
    if (!queue || !tasks || count == 0) {
        if (enqueued) *enqueued = 0;
        return false;
    }
    
    uint32_t head = ATOMIC_LOAD(&queue->head);
    uint32_t tail = ATOMIC_LOAD(&queue->tail);
    
    // 计算可用空间
    uint32_t available = queue->capacity - ((tail - head) & queue->mask) - 1;
    uint32_t to_enqueue = (count < available) ? count : available;
    
    if (to_enqueue == 0) {
        if (enqueued) *enqueued = 0;
        return false;
    }
    
    // 批量写入任务
    for (uint32_t i = 0; i < to_enqueue; i++) {
        queue->tasks[(tail + i) & queue->mask] = tasks[i];
    }
    
    // 内存屏障
    MEMORY_BARRIER();
    
    // 更新tail指针
    ATOMIC_STORE(&queue->tail, tail + to_enqueue);
    
    if (enqueued) *enqueued = to_enqueue;
    return true;
}

/**
 * 批量出队操作
 */
uint32_t queue_dequeue_batch(lockfree_queue_t* queue,
                             task_internal_t** tasks,
                             uint32_t max_count) {
    if (!queue || !tasks || max_count == 0) {
        return 0;
    }
    
    uint32_t head = ATOMIC_LOAD(&queue->head);
    uint32_t tail = ATOMIC_LOAD(&queue->tail);
    
    // 计算可读取的元素数量
    uint32_t available = (tail - head) & queue->mask;
    uint32_t to_dequeue = (max_count < available) ? max_count : available;
    
    if (to_dequeue == 0) {
        return 0;
    }
    
    // 批量读取任务
    for (uint32_t i = 0; i < to_dequeue; i++) {
        uint32_t index = (head + i) & queue->mask;
        tasks[i] = queue->tasks[index];
        queue->tasks[index] = NULL;
    }
    
    // 内存屏障
    MEMORY_BARRIER();
    
    // 更新head指针
    ATOMIC_STORE(&queue->head, head + to_dequeue);
    
    return to_dequeue;
}

// ============================================================================
// 队列统计和调试接口
// ============================================================================

/**
 * 获取队列详细统计信息
 */
typedef struct {
    uint32_t capacity;              // 队列容量
    uint32_t size;                  // 当前大小
    uint32_t head_pos;              // 头指针位置
    uint32_t tail_pos;              // 尾指针位置
    double utilization;             // 利用率
    bool is_empty;                  // 是否为空
    bool is_full;                   // 是否已满
} queue_stats_t;

void queue_get_stats(lockfree_queue_t* queue, queue_stats_t* stats) {
    if (!queue || !stats) {
        return;
    }
    
    uint32_t head = ATOMIC_LOAD(&queue->head);
    uint32_t tail = ATOMIC_LOAD(&queue->tail);
    uint32_t size = (tail - head) & queue->mask;
    
    stats->capacity = queue->capacity;
    stats->size = size;
    stats->head_pos = head & queue->mask;
    stats->tail_pos = tail & queue->mask;
    stats->utilization = (double)size / (queue->capacity - 1);
    stats->is_empty = (size == 0);
    stats->is_full = (size == queue->capacity - 1);
}

/**
 * 调试用：打印队列状态
 */
void queue_dump_state(lockfree_queue_t* queue) {
    if (!queue) {
        printf("Queue: NULL\n");
        return;
    }
    
    queue_stats_t stats;
    queue_get_stats(queue, &stats);
    
    printf("Queue State:\n");
    printf("  Capacity: %u\n", stats.capacity);
    printf("  Size: %u\n", stats.size);
    printf("  Head: %u\n", stats.head_pos);
    printf("  Tail: %u\n", stats.tail_pos);
    printf("  Utilization: %.2f%%\n", stats.utilization * 100.0);
    printf("  Empty: %s\n", stats.is_empty ? "Yes" : "No");
    printf("  Full: %s\n", stats.is_full ? "Yes" : "No");
}

// ============================================================================
// 性能测试接口
// ============================================================================

/**
 * 队列性能测试
 */
typedef struct {
    uint64_t enqueue_ops;           // 入队操作数
    uint64_t dequeue_ops;           // 出队操作数
    uint64_t enqueue_time_ns;       // 入队总时间
    uint64_t dequeue_time_ns;       // 出队总时间
    double enqueue_throughput;      // 入队吞吐量(ops/sec)
    double dequeue_throughput;      // 出队吞吐量(ops/sec)
} queue_perf_t;

void queue_benchmark(lockfree_queue_t* queue, 
                    uint32_t iterations,
                    queue_perf_t* perf) {
    if (!queue || !perf || iterations == 0) {
        return;
    }
    
    memset(perf, 0, sizeof(queue_perf_t));
    
    // 创建测试任务
    task_internal_t test_task;
    memset(&test_task, 0, sizeof(test_task));
    test_task.task_id = 1;
    
    // 测试入队性能
    uint64_t start_time = get_time_ns();
    for (uint32_t i = 0; i < iterations; i++) {
        if (queue_enqueue(queue, &test_task)) {
            perf->enqueue_ops++;
        }
        
        // 防止队列满
        if (queue_is_full(queue)) {
            queue_dequeue(queue);
        }
    }
    perf->enqueue_time_ns = get_time_ns() - start_time;
    
    // 清空队列
    while (!queue_is_empty(queue)) {
        queue_dequeue(queue);
    }
    
    // 先填充队列
    for (uint32_t i = 0; i < iterations && !queue_is_full(queue); i++) {
        queue_enqueue(queue, &test_task);
    }
    
    // 测试出队性能
    start_time = get_time_ns();
    for (uint32_t i = 0; i < iterations; i++) {
        if (queue_dequeue(queue)) {
            perf->dequeue_ops++;
        }
        
        // 防止队列空
        if (queue_is_empty(queue)) {
            queue_enqueue(queue, &test_task);
        }
    }
    perf->dequeue_time_ns = get_time_ns() - start_time;
    
    // 计算吞吐量
    if (perf->enqueue_time_ns > 0) {
        perf->enqueue_throughput = (double)perf->enqueue_ops * 1000000000.0 / perf->enqueue_time_ns;
    }
    
    if (perf->dequeue_time_ns > 0) {
        perf->dequeue_throughput = (double)perf->dequeue_ops * 1000000000.0 / perf->dequeue_time_ns;
    }
}