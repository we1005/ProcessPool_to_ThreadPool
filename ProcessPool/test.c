#include "process_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

/* 测试计数器 */
static int tests_run = 0;
static int tests_passed = 0;

/* 测试宏 */
#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf("[PASS] %s\n", message); \
    } else { \
        printf("[FAIL] %s\n", message); \
    } \
} while(0)

/**
 * 简单的测试任务处理函数
 * 将输入字符串反转
 */
int reverse_string_handler(const char* task_data, int data_len, char* result_data, int* result_len) {
    if (data_len <= 0 || data_len >= MAX_TASK_DATA) {
        return -1;
    }
    
    /* 反转字符串 */
    for (int i = 0; i < data_len; i++) {
        result_data[i] = task_data[data_len - 1 - i];
    }
    
    *result_len = data_len;
    result_data[*result_len] = '\0';
    
    return 0;
}

/**
 * 测试进程池创建和销毁
 */
void test_pool_creation_destruction(void) {
    printf("\n=== 测试进程池创建和销毁 ===\n");
    
    /* 测试正常创建 */
    process_pool_t* pool = process_pool_create(2, NULL);
    TEST_ASSERT(pool != NULL, "进程池创建成功");
    
    if (pool) {
        /* 测试状态获取 */
        int active_workers, pending_tasks;
        process_pool_get_status(pool, &active_workers, &pending_tasks);
        TEST_ASSERT(active_workers == 2, "工作进程数量正确");
        TEST_ASSERT(pending_tasks == 0, "初始待处理任务数为0");
        
        /* 销毁进程池 */
        process_pool_destroy(pool);
        printf("进程池销毁完成\n");
    }
    
    /* 测试无效参数 */
    process_pool_t* invalid_pool = process_pool_create(0, NULL);
    TEST_ASSERT(invalid_pool == NULL, "无效工作进程数创建失败");
    
    invalid_pool = process_pool_create(MAX_WORKERS + 1, NULL);
    TEST_ASSERT(invalid_pool == NULL, "超出最大工作进程数创建失败");
}

/**
 * 测试任务提交和结果获取
 */
void test_task_submission_and_results(void) {
    printf("\n=== 测试任务提交和结果获取 ===\n");
    
    process_pool_t* pool = process_pool_create(2, reverse_string_handler);
    TEST_ASSERT(pool != NULL, "进程池创建成功");
    
    if (pool == NULL) {
        return;
    }
    
    /* 启动进程池 */
    pid_t runner_pid = fork();
    if (runner_pid == 0) {
        process_pool_run(pool);
        exit(0);
    }
    
    /* 提交任务 */
    const char* test_string = "hello";
    int task_id = process_pool_submit_task(pool, test_string, strlen(test_string));
    TEST_ASSERT(task_id > 0, "任务提交成功");
    
    /* 获取结果 */
    task_result_t result;
    int get_result = process_pool_get_result(pool, &result, 3000);
    TEST_ASSERT(get_result == 0, "成功获取任务结果");
    
    if (get_result == 0) {
        TEST_ASSERT(result.task_id == task_id, "任务ID匹配");
        TEST_ASSERT(result.result_code == 0, "任务执行成功");
        TEST_ASSERT(strcmp(result.result_data, "olleh") == 0, "字符串反转正确");
        printf("原字符串: %s, 反转后: %.*s\n", test_string, result.result_len, result.result_data);
    }
    
    /* 测试无效任务提交 */
    int invalid_task = process_pool_submit_task(pool, NULL, 0);
    TEST_ASSERT(invalid_task == -1, "无效任务提交失败");
    
    /* 清理 */
    process_pool_stop(pool);
    kill(runner_pid, SIGTERM);
    waitpid(runner_pid, NULL, 0);
    process_pool_destroy(pool);
}

/**
 * 测试多任务并发处理
 */
void test_concurrent_tasks(void) {
    printf("\n=== 测试多任务并发处理 ===\n");
    
    process_pool_t* pool = process_pool_create(3, reverse_string_handler);
    TEST_ASSERT(pool != NULL, "进程池创建成功");
    
    if (pool == NULL) {
        return;
    }
    
    /* 启动进程池 */
    pid_t runner_pid = fork();
    if (runner_pid == 0) {
        process_pool_run(pool);
        exit(0);
    }
    
    /* 提交多个任务 */
    const char* test_strings[] = {
        "abc",
        "def",
        "ghi",
        "jkl",
        "mno"
    };
    const char* expected_results[] = {
        "cba",
        "fed",
        "ihg",
        "lkj",
        "onm"
    };
    
    int task_count = sizeof(test_strings) / sizeof(test_strings[0]);
    int submitted_tasks = 0;
    
    /* 提交所有任务 */
    for (int i = 0; i < task_count; i++) {
        int task_id = process_pool_submit_task(pool, test_strings[i], strlen(test_strings[i]));
        if (task_id > 0) {
            submitted_tasks++;
        }
    }
    
    TEST_ASSERT(submitted_tasks == task_count, "所有任务提交成功");
    
    /* 获取所有结果 */
    int received_results = 0;
    int correct_results = 0;
    
    for (int i = 0; i < submitted_tasks; i++) {
        task_result_t result;
        if (process_pool_get_result(pool, &result, 5000) == 0) {
            received_results++;
            
            /* 检查结果是否在预期结果中 */
            for (int j = 0; j < task_count; j++) {
                if (strcmp(result.result_data, expected_results[j]) == 0) {
                    correct_results++;
                    break;
                }
            }
            
            printf("收到结果: %.*s\n", result.result_len, result.result_data);
        }
    }
    
    TEST_ASSERT(received_results == submitted_tasks, "收到所有任务结果");
    TEST_ASSERT(correct_results == submitted_tasks, "所有结果都正确");
    
    /* 清理 */
    process_pool_stop(pool);
    kill(runner_pid, SIGTERM);
    waitpid(runner_pid, NULL, 0);
    process_pool_destroy(pool);
}

/**
 * 测试错误处理
 */
void test_error_handling(void) {
    printf("\n=== 测试错误处理 ===\n");
    
    /* 测试NULL指针 */
    int result = process_pool_submit_task(NULL, "test", 4);
    TEST_ASSERT(result == -1, "NULL进程池提交任务失败");
    
    task_result_t task_result;
    result = process_pool_get_result(NULL, &task_result, 1000);
    TEST_ASSERT(result == -1, "NULL进程池获取结果失败");
    
    process_pool_get_status(NULL, NULL, NULL);
    printf("[PASS] NULL进程池状态查询不崩溃\n");
    tests_run++;
    tests_passed++;
    
    process_pool_stop(NULL);
    printf("[PASS] NULL进程池停止不崩溃\n");
    tests_run++;
    tests_passed++;
    
    process_pool_destroy(NULL);
    printf("[PASS] NULL进程池销毁不崩溃\n");
    tests_run++;
    tests_passed++;
}

/**
 * 测试超时处理
 */
void test_timeout_handling(void) {
    printf("\n=== 测试超时处理 ===\n");
    
    process_pool_t* pool = process_pool_create(1, NULL);
    TEST_ASSERT(pool != NULL, "进程池创建成功");
    
    if (pool == NULL) {
        return;
    }
    
    /* 启动进程池 */
    pid_t runner_pid = fork();
    if (runner_pid == 0) {
        process_pool_run(pool);
        exit(0);
    }
    
    /* 尝试获取不存在的结果（应该超时） */
    task_result_t result;
    clock_t start_time = clock();
    int get_result = process_pool_get_result(pool, &result, 1000);  /* 1秒超时 */
    clock_t end_time = clock();
    
    double elapsed = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    TEST_ASSERT(get_result == -1, "获取不存在结果超时失败");
    TEST_ASSERT(elapsed >= 0.9 && elapsed <= 1.5, "超时时间大致正确");
    
    printf("超时测试耗时: %.2f 秒\n", elapsed);
    
    /* 清理 */
    process_pool_stop(pool);
    kill(runner_pid, SIGTERM);
    waitpid(runner_pid, NULL, 0);
    process_pool_destroy(pool);
}

/**
 * 运行所有测试
 */
int main(void) {
    printf("进程池测试程序\n");
    printf("================\n");
    
    /* 运行测试 */
    test_pool_creation_destruction();
    test_task_submission_and_results();
    test_concurrent_tasks();
    test_error_handling();
    test_timeout_handling();
    
    /* 输出测试结果 */
    printf("\n=== 测试结果 ===\n");
    printf("总测试数: %d\n", tests_run);
    printf("通过测试: %d\n", tests_passed);
    printf("失败测试: %d\n", tests_run - tests_passed);
    printf("成功率: %.1f%%\n", (double)tests_passed / tests_run * 100);
    
    if (tests_passed == tests_run) {
        printf("\n🎉 所有测试通过！\n");
        return 0;
    } else {
        printf("\n❌ 有测试失败！\n");
        return 1;
    }
}