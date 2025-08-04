#include "process_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/**
 * 自定义任务处理函数示例
 * 这个函数计算字符串中字符的频率
 */
int char_frequency_handler(const char* task_data, int data_len, char* result_data, int* result_len) {
    if (data_len <= 0 || data_len >= MAX_TASK_DATA) {
        return -1;
    }
    
    /* 统计字符频率 */
    int freq[256] = {0};
    for (int i = 0; i < data_len; i++) {
        freq[(unsigned char)task_data[i]]++;
    }
    
    /* 格式化结果 */
    int pos = 0;
    pos += snprintf(result_data + pos, MAX_TASK_DATA - pos, "Character frequencies for '%.*s':\n", data_len, task_data);
    
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0 && pos < MAX_TASK_DATA - 50) {
            if (i >= 32 && i <= 126) {  /* 可打印字符 */
                pos += snprintf(result_data + pos, MAX_TASK_DATA - pos, "'%c': %d\n", i, freq[i]);
            } else {
                pos += snprintf(result_data + pos, MAX_TASK_DATA - pos, "\\x%02x: %d\n", i, freq[i]);
            }
        }
    }
    
    *result_len = pos;
    return 0;
}

/**
 * 数学计算任务处理函数示例
 * 计算斐波那契数列的第n项
 */
int fibonacci_handler(const char* task_data, int data_len, char* result_data, int* result_len) {
    int n = atoi(task_data);
    
    if (n < 0 || n > 40) {  /* 限制范围避免计算时间过长 */
        *result_len = snprintf(result_data, MAX_TASK_DATA, "Error: n must be between 0 and 40");
        return -1;
    }
    
    /* 计算斐波那契数 */
    long long fib = 0, a = 0, b = 1;
    
    if (n == 0) {
        fib = 0;
    } else if (n == 1) {
        fib = 1;
    } else {
        for (int i = 2; i <= n; i++) {
            fib = a + b;
            a = b;
            b = fib;
        }
    }
    
    *result_len = snprintf(result_data, MAX_TASK_DATA, "fibonacci(%d) = %lld", n, fib);
    return 0;
}

/**
 * 演示基本用法
 */
void demo_basic_usage(void) {
    printf("\n=== 基本用法演示 ===\n");
    
    /* 创建进程池，4个工作进程，使用默认处理函数 */
    process_pool_t* pool = process_pool_create(4, NULL);
    if (pool == NULL) {
        printf("Failed to create process pool\n");
        return;
    }
    
    /* 提交一些任务 */
    const char* tasks[] = {
        "hello world",
        "process pool",
        "linux programming",
        "concurrent processing",
        "task distribution"
    };
    
    int task_count = sizeof(tasks) / sizeof(tasks[0]);
    int task_ids[task_count];
    
    printf("提交 %d 个任务...\n", task_count);
    for (int i = 0; i < task_count; i++) {
        task_ids[i] = process_pool_submit_task(pool, tasks[i], strlen(tasks[i]));
        if (task_ids[i] == -1) {
            printf("Failed to submit task %d\n", i);
        } else {
            printf("Task %d submitted: %s\n", task_ids[i], tasks[i]);
        }
    }
    
    /* 启动进程池（在后台线程中运行） */
    pid_t runner_pid = fork();
    if (runner_pid == 0) {
        /* 子进程运行进程池 */
        process_pool_run(pool);
        exit(0);
    }
    
    /* 获取结果 */
    printf("\n获取任务结果...\n");
    task_result_t result;
    for (int i = 0; i < task_count; i++) {
        if (process_pool_get_result(pool, &result, 5000) == 0) {  /* 5秒超时 */
            printf("Task %d result (code %d): %.*s\n", 
                   result.task_id, result.result_code, 
                   result.result_len, result.result_data);
        } else {
            printf("Failed to get result for task\n");
        }
    }
    
    /* 停止并销毁进程池 */
    process_pool_stop(pool);
    kill(runner_pid, SIGTERM);
    waitpid(runner_pid, NULL, 0);
    process_pool_destroy(pool);
    
    printf("基本用法演示完成\n");
}

/**
 * 演示自定义处理函数
 */
void demo_custom_handler(void) {
    printf("\n=== 自定义处理函数演示 ===\n");
    
    /* 创建进程池，使用字符频率统计处理函数 */
    process_pool_t* pool = process_pool_create(2, char_frequency_handler);
    if (pool == NULL) {
        printf("Failed to create process pool\n");
        return;
    }
    
    /* 提交任务 */
    const char* texts[] = {
        "hello",
        "world",
        "programming"
    };
    
    int task_count = sizeof(texts) / sizeof(texts[0]);
    
    printf("提交字符频率统计任务...\n");
    for (int i = 0; i < task_count; i++) {
        int task_id = process_pool_submit_task(pool, texts[i], strlen(texts[i]));
        printf("Task %d submitted: %s\n", task_id, texts[i]);
    }
    
    /* 启动进程池 */
    pid_t runner_pid = fork();
    if (runner_pid == 0) {
        process_pool_run(pool);
        exit(0);
    }
    
    /* 获取结果 */
    printf("\n获取统计结果...\n");
    task_result_t result;
    for (int i = 0; i < task_count; i++) {
        if (process_pool_get_result(pool, &result, 3000) == 0) {
            printf("\nTask %d result:\n%.*s\n", 
                   result.task_id, result.result_len, result.result_data);
        }
    }
    
    /* 清理 */
    process_pool_stop(pool);
    kill(runner_pid, SIGTERM);
    waitpid(runner_pid, NULL, 0);
    process_pool_destroy(pool);
    
    printf("自定义处理函数演示完成\n");
}

/**
 * 演示性能测试
 */
void demo_performance_test(void) {
    printf("\n=== 性能测试演示 ===\n");
    
    /* 创建进程池，使用斐波那契计算处理函数 */
    process_pool_t* pool = process_pool_create(4, fibonacci_handler);
    if (pool == NULL) {
        printf("Failed to create process pool\n");
        return;
    }
    
    /* 准备任务数据 */
    const int task_count = 20;
    char task_data[task_count][16];
    
    printf("提交 %d 个斐波那契计算任务...\n", task_count);
    clock_t start_time = clock();
    
    for (int i = 0; i < task_count; i++) {
        snprintf(task_data[i], sizeof(task_data[i]), "%d", 20 + i);
        int task_id = process_pool_submit_task(pool, task_data[i], strlen(task_data[i]));
        printf("Task %d: fibonacci(%s)\n", task_id, task_data[i]);
    }
    
    /* 启动进程池 */
    pid_t runner_pid = fork();
    if (runner_pid == 0) {
        process_pool_run(pool);
        exit(0);
    }
    
    /* 获取所有结果 */
    printf("\n计算结果...\n");
    task_result_t result;
    int completed = 0;
    
    while (completed < task_count) {
        if (process_pool_get_result(pool, &result, 1000) == 0) {
            printf("Result: %.*s\n", result.result_len, result.result_data);
            completed++;
        } else {
            printf("Timeout waiting for result\n");
            break;
        }
    }
    
    clock_t end_time = clock();
    double elapsed = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    printf("\n性能统计:\n");
    printf("完成任务数: %d/%d\n", completed, task_count);
    printf("总耗时: %.2f 秒\n", elapsed);
    printf("平均每任务: %.2f 秒\n", elapsed / completed);
    
    /* 获取进程池状态 */
    int active_workers, pending_tasks;
    process_pool_get_status(pool, &active_workers, &pending_tasks);
    printf("活跃工作进程: %d\n", active_workers);
    printf("待处理任务: %d\n", pending_tasks);
    
    /* 清理 */
    process_pool_stop(pool);
    kill(runner_pid, SIGTERM);
    waitpid(runner_pid, NULL, 0);
    process_pool_destroy(pool);
    
    printf("性能测试演示完成\n");
}

int main(int argc, char* argv[]) {
    printf("进程池演示程序\n");
    printf("================\n");
    
    /* 演示基本用法 */
    demo_basic_usage();
    
    sleep(1);
    
    /* 演示自定义处理函数 */
    demo_custom_handler();
    
    sleep(1);
    
    /* 演示性能测试 */
    demo_performance_test();
    
    printf("\n所有演示完成！\n");
    return 0;
}