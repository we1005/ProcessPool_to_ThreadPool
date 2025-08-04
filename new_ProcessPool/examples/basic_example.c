#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "../include/process_pool.h"

// 简单的计算任务：计算斐波那契数列
static int fibonacci_task(void* input_data, size_t input_size, 
                         void** output_data, size_t* output_size, void* user_data) {
    if (!input_data || input_size != sizeof(int)) {
        return -1;
    }
    
    int n = *(int*)input_data;
    
    // 模拟一些计算时间
    usleep(10000); // 10ms
    
    // 计算斐波那契数
    long long result = 1;
    if (n > 1) {
        long long a = 0, b = 1;
        for (int i = 2; i <= n; i++) {
            result = a + b;
            a = b;
            b = result;
        }
    } else if (n == 0) {
        result = 0;
    }
    
    // 分配输出内存
    long long* output = malloc(sizeof(long long));
    if (!output) {
        return -1;
    }
    
    *output = result;
    *output_data = output;
    *output_size = sizeof(long long);
    
    printf("Worker computed fibonacci(%d) = %lld\n", n, result);
    
    return 0;
}

// 任务完成回调函数
static void task_completion_callback(task_future_t* future, void* user_data) {
    task_result_t result;
    if (pool_future_wait(future, &result, 0) == 0) {
        if (result.status == TASK_STATUS_COMPLETED && result.output_data) {
            long long value = *(long long*)result.output_data;
            printf("Task completed with result: %lld\n", value);
            free(result.output_data);
        } else {
            printf("Task failed with error: %s\n", result.error_message);
        }
    }
    
    pool_future_destroy(future);
}

int main(int argc, char* argv[]) {
    printf("=== Process Pool Basic Example ===\n");
    printf("Process Pool Version: %s\n", pool_get_version());
    
    // 创建进程池配置
    pool_config_t config = {
        .worker_count = 4,
        .max_workers = 8,
        .min_workers = 2,
        .queue_size = 100,
        .worker_idle_timeout = 30000,  // 30秒
        .task_timeout = 10000,         // 10秒
        .enable_dynamic_scaling = true,
        .enable_worker_affinity = false,
        .log_level = 2,  // INFO
        .pool_name = "fibonacci_pool"
    };
    
    // 创建进程池
    printf("\nCreating process pool...\n");
    process_pool_t* pool = pool_create(&config);
    if (!pool) {
        fprintf(stderr, "Failed to create process pool: %s\n", strerror(errno));
        return 1;
    }
    
    // 启动进程池
    printf("Starting process pool...\n");
    if (pool_start(pool) != 0) {
        fprintf(stderr, "Failed to start process pool: %s\n", strerror(errno));
        pool_destroy(pool);
        return 1;
    }
    
    printf("Process pool started successfully!\n");
    
    // 提交一些同步任务
    printf("\n=== Synchronous Tasks ===\n");
    for (int i = 1; i <= 5; i++) {
        printf("Submitting synchronous task for fibonacci(%d)...\n", i * 5);
        
        int input = i * 5;
        task_result_t result;
        
        int ret = pool_submit_sync(pool, fibonacci_task, &input, sizeof(input), 
                                  NULL, TASK_PRIORITY_NORMAL, 5000, &result);
        
        if (ret == 0 && result.status == TASK_STATUS_COMPLETED) {
            long long value = *(long long*)result.output_data;
            printf("Synchronous result: fibonacci(%d) = %lld\n", input, value);
            free(result.output_data);
        } else {
            printf("Synchronous task failed: %s\n", result.error_message);
        }
    }
    
    // 提交一些异步任务
    printf("\n=== Asynchronous Tasks ===\n");
    task_future_t* futures[10];
    int future_count = 0;
    
    for (int i = 1; i <= 10; i++) {
        printf("Submitting asynchronous task for fibonacci(%d)...\n", i * 3);
        
        int input = i * 3;
        task_future_t* future = pool_submit_async(pool, fibonacci_task, &input, sizeof(input),
                                                 NULL, TASK_PRIORITY_NORMAL, 0);
        
        if (future) {
            futures[future_count++] = future;
        } else {
            printf("Failed to submit asynchronous task\n");
        }
    }
    
    // 等待异步任务完成
    printf("\nWaiting for asynchronous tasks to complete...\n");
    for (int i = 0; i < future_count; i++) {
        task_result_t result;
        if (pool_future_wait(futures[i], &result, 10000) == 0) {
            if (result.status == TASK_STATUS_COMPLETED && result.output_data) {
                long long value = *(long long*)result.output_data;
                printf("Async result %d: %lld\n", i + 1, value);
                free(result.output_data);
            } else {
                printf("Async task %d failed: %s\n", i + 1, result.error_message);
            }
        } else {
            printf("Async task %d timed out\n", i + 1);
        }
        
        pool_future_destroy(futures[i]);
    }
    
    // 批量提交任务
    printf("\n=== Batch Tasks ===\n");
    task_desc_t batch_tasks[5];
    int batch_inputs[5] = {10, 15, 20, 25, 30};
    
    for (int i = 0; i < 5; i++) {
        batch_tasks[i].task_func = fibonacci_task;
        batch_tasks[i].input_data = &batch_inputs[i];
        batch_tasks[i].input_size = sizeof(int);
        batch_tasks[i].user_data = NULL;
        batch_tasks[i].priority = TASK_PRIORITY_NORMAL;
        batch_tasks[i].timeout_ms = 5000;
        batch_tasks[i].completion_callback = task_completion_callback;
        batch_tasks[i].callback_data = NULL;
    }
    
    task_future_t** batch_futures = pool_submit_batch(pool, batch_tasks, 5);
    if (batch_futures) {
        printf("Submitted batch of 5 tasks\n");
        
        // 等待批量任务完成
        for (int i = 0; i < 5; i++) {
            if (batch_futures[i]) {
                task_result_t result;
                if (pool_future_wait(batch_futures[i], &result, 10000) == 0) {
                    if (result.status == TASK_STATUS_COMPLETED && result.output_data) {
                        long long value = *(long long*)result.output_data;
                        printf("Batch result %d: fibonacci(%d) = %lld\n", 
                               i + 1, batch_inputs[i], value);
                        free(result.output_data);
                    }
                }
                pool_future_destroy(batch_futures[i]);
            }
        }
        
        free(batch_futures);
    } else {
        printf("Failed to submit batch tasks\n");
    }
    
    // 显示统计信息
    printf("\n=== Pool Statistics ===\n");
    pool_stats_t stats;
    if (pool_get_stats(pool, &stats) == 0) {
        printf("Tasks submitted: %lu\n", stats.tasks_submitted);
        printf("Tasks completed: %lu\n", stats.tasks_completed);
        printf("Tasks failed: %lu\n", stats.tasks_failed);
        printf("Tasks cancelled: %lu\n", stats.tasks_cancelled);
        printf("Active workers: %d\n", stats.active_workers);
        printf("Idle workers: %d\n", stats.idle_workers);
        printf("Queue size: %d\n", stats.queue_size);
        printf("Average task time: %.2f ms\n", stats.avg_task_time_ms);
        printf("Average queue time: %.2f ms\n", stats.avg_queue_time_ms);
        printf("Pool uptime: %.2f seconds\n", stats.uptime_seconds);
    }
    
    // 显示Worker信息
    printf("\n=== Worker Information ===\n");
    worker_info_t* workers;
    int worker_count;
    if (pool_get_workers(pool, &workers, &worker_count) == 0) {
        for (int i = 0; i < worker_count; i++) {
            printf("Worker %d: PID=%d, Status=%d, Tasks=%lu, CPU=%.1f%%\n",
                   i, workers[i].pid, workers[i].status, 
                   workers[i].tasks_completed, workers[i].cpu_usage);
        }
        free(workers);
    }
    
    // 测试动态扩缩容
    printf("\n=== Dynamic Scaling Test ===\n");
    printf("Current worker count: %d\n", stats.active_workers + stats.idle_workers);
    
    printf("Scaling up to 6 workers...\n");
    if (pool_resize(pool, 6) == 0) {
        sleep(2);  // 等待扩容完成
        if (pool_get_stats(pool, &stats) == 0) {
            printf("New worker count: %d\n", stats.active_workers + stats.idle_workers);
        }
    }
    
    printf("Scaling down to 3 workers...\n");
    if (pool_resize(pool, 3) == 0) {
        sleep(2);  // 等待缩容完成
        if (pool_get_stats(pool, &stats) == 0) {
            printf("New worker count: %d\n", stats.active_workers + stats.idle_workers);
        }
    }
    
    // 停止和销毁进程池
    printf("\n=== Cleanup ===\n");
    printf("Stopping process pool...\n");
    if (pool_stop(pool, 5000) != 0) {
        printf("Warning: Pool stop timed out, forcing shutdown\n");
    }
    
    printf("Destroying process pool...\n");
    pool_destroy(pool);
    
    printf("Example completed successfully!\n");
    return 0;
}

// 编译命令示例:
// gcc -o basic_example basic_example.c -L../build -lprocesspool -lpthread -lrt