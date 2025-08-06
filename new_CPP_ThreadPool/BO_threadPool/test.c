#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

/* 测试任务结构 */
typedef struct {
    int task_id;
    int sleep_time;
} test_task_arg_t;

/* 简单任务函数 */
void simple_task(void* arg) {
    test_task_arg_t* task_arg = (test_task_arg_t*)arg;
    printf("Task %d started, will sleep for %d ms\n", task_arg->task_id, task_arg->sleep_time);
    usleep(task_arg->sleep_time * 1000); // 转换为微秒
    printf("Task %d completed\n", task_arg->task_id);
}

/* 计算密集型任务 */
void compute_task(void* arg) {
    int* task_id = (int*)arg;
    long long sum = 0;
    int i;
    
    printf("Compute task %d started\n", *task_id);
    
    /* 执行一些计算密集型操作 */
    for (i = 0; i < 1000000; i++) {
        sum += i * i;
    }
    
    printf("Compute task %d completed, result: %lld\n", *task_id, sum);
}

/* 获取当前时间（毫秒） */
long long get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

/* 基本功能测试 */
void test_basic_functionality() {
    printf("\n=== 基本功能测试 ===\n");
    
    threadpool_t* pool = threadpool_create(4, 10);
    if (pool == NULL) {
        printf("Failed to create thread pool\n");
        return;
    }
    
    printf("Thread pool created with %d threads\n", threadpool_thread_count(pool));
    
    /* 添加一些简单任务 */
    test_task_arg_t tasks[5];
    int i;
    
    for (i = 0; i < 5; i++) {
        tasks[i].task_id = i + 1;
        tasks[i].sleep_time = (i + 1) * 100; // 100ms, 200ms, 300ms, 400ms, 500ms
        
        if (threadpool_add(pool, simple_task, &tasks[i]) != THREADPOOL_SUCCESS) {
            printf("Failed to add task %d\n", i + 1);
        } else {
            printf("Task %d added to queue\n", i + 1);
        }
    }
    
    printf("Current queue count: %d\n", threadpool_queue_count(pool));
    
    /* 等待任务完成 */
    sleep(3);
    
    /* 销毁线程池 */
    if (threadpool_destroy(pool) == THREADPOOL_SUCCESS) {
        printf("Thread pool destroyed successfully\n");
    } else {
        printf("Failed to destroy thread pool\n");
    }
}

/* 性能测试 */
void test_performance() {
    printf("\n=== 性能测试 ===\n");
    
    const int thread_count = 8;
    const int task_count = 100;
    
    threadpool_t* pool = threadpool_create(thread_count, task_count);
    if (pool == NULL) {
        printf("Failed to create thread pool\n");
        return;
    }
    
    printf("Performance test: %d threads, %d compute tasks\n", thread_count, task_count);
    
    long long start_time = get_time_ms();
    
    /* 添加计算密集型任务 */
    int task_ids[task_count];
    int i;
    
    for (i = 0; i < task_count; i++) {
        task_ids[i] = i + 1;
        if (threadpool_add(pool, compute_task, &task_ids[i]) != THREADPOOL_SUCCESS) {
            printf("Failed to add compute task %d\n", i + 1);
        }
    }
    
    printf("All tasks added, waiting for completion...\n");
    
    /* 等待所有任务完成 */
    while (threadpool_queue_count(pool) > 0) {
        usleep(10000); // 10ms
    }
    
    /* 额外等待确保所有任务执行完成 */
    sleep(2);
    
    long long end_time = get_time_ms();
    
    printf("Performance test completed in %lld ms\n", end_time - start_time);
    
    threadpool_destroy(pool);
}

/* 压力测试 */
void test_stress() {
    printf("\n=== 压力测试 ===\n");
    
    threadpool_t* pool = threadpool_create(4, 20);
    if (pool == NULL) {
        printf("Failed to create thread pool\n");
        return;
    }
    
    printf("Stress test: adding tasks rapidly\n");
    
    /* 快速添加大量任务 */
    test_task_arg_t stress_tasks[50];
    int i, success_count = 0;
    
    for (i = 0; i < 50; i++) {
        stress_tasks[i].task_id = i + 1;
        stress_tasks[i].sleep_time = 50; // 50ms
        
        int result = threadpool_add(pool, simple_task, &stress_tasks[i]);
        if (result == THREADPOOL_SUCCESS) {
            success_count++;
        } else if (result == THREADPOOL_QUEUE_FULL) {
            printf("Queue full at task %d\n", i + 1);
            usleep(100000); // 等待100ms后重试
            i--; // 重试当前任务
        } else {
            printf("Failed to add task %d, error: %d\n", i + 1, result);
        }
    }
    
    printf("Successfully added %d tasks\n", success_count);
    
    /* 等待任务完成 */
    sleep(5);
    
    threadpool_destroy(pool);
}

/* 错误处理测试 */
void test_error_handling() {
    printf("\n=== 错误处理测试 ===\n");
    
    /* 测试无效参数 */
    threadpool_t* pool = threadpool_create(0, 10);
    if (pool == NULL) {
        printf("✓ Correctly rejected invalid thread count\n");
    }
    
    pool = threadpool_create(10, 0);
    if (pool == NULL) {
        printf("✓ Correctly rejected invalid queue size\n");
    }
    
    /* 测试正常创建 */
    pool = threadpool_create(2, 5);
    if (pool != NULL) {
        printf("✓ Thread pool created successfully\n");
        
        /* 测试添加NULL函数 */
        if (threadpool_add(pool, NULL, NULL) == THREADPOOL_INVALID) {
            printf("✓ Correctly rejected NULL function\n");
        }
        
        /* 测试重复销毁 */
        threadpool_destroy(pool);
        if (threadpool_destroy(pool) == THREADPOOL_INVALID) {
            printf("✓ Correctly handled double destroy\n");
        }
    }
}

int main() {
    printf("基于对象的线程池测试程序\n");
    printf("================================\n");
    
    test_basic_functionality();
    test_performance();
    test_stress();
    test_error_handling();
    
    printf("\n所有测试完成\n");
    
    return 0;
}