#include "ThreadPool.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>
#include <string>
#include <sstream>

// 获取当前时间戳（毫秒）
long long get_timestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

// 简单任务函数
void simple_task(int task_id, int sleep_ms) {
    std::cout << "Task " << task_id << " started, will sleep for " << sleep_ms << " ms" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    std::cout << "Task " << task_id << " completed" << std::endl;
}

// 计算任务函数
long long compute_task(int task_id, int iterations) {
    std::cout << "Compute task " << task_id << " started with " << iterations << " iterations" << std::endl;
    
    long long sum = 0;
    for (int i = 0; i < iterations; ++i) {
        sum += i * i;
    }
    
    std::cout << "Compute task " << task_id << " completed, result: " << sum << std::endl;
    return sum;
}

// 字符串处理任务
std::string string_task(const std::string& input, int repeat) {
    std::stringstream ss;
    for (int i = 0; i < repeat; ++i) {
        ss << input << "_" << i;
        if (i < repeat - 1) ss << "|";
    }
    return ss.str();
}

// Lambda任务示例
auto create_lambda_task(int multiplier) {
    return [multiplier](int value) -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return value * multiplier;
    };
}

// 基本功能测试
void test_basic_functionality() {
    std::cout << "\n=== 基本功能测试 ===" << std::endl;
    
    try {
        ThreadPool pool(4);
        std::cout << "Thread pool created with " << pool.size() << " threads" << std::endl;
        
        // 提交一些简单任务
        std::vector<std::future<void>> results;
        
        for (int i = 0; i < 5; ++i) {
            auto future = pool.enqueue(simple_task, i + 1, (i + 1) * 100);
            results.push_back(std::move(future));
        }
        
        std::cout << "Current queue size: " << pool.queue_size() << std::endl;
        std::cout << "Active threads: " << pool.active_count() << std::endl;
        
        // 等待所有任务完成
        for (auto& result : results) {
            result.wait();
        }
        
        std::cout << "All tasks completed" << std::endl;
        std::cout << "Total completed tasks: " << pool.completed_task_count() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in basic functionality test: " << e.what() << std::endl;
    }
}

// 返回值测试
void test_return_values() {
    std::cout << "\n=== 返回值测试 ===" << std::endl;
    
    try {
        ThreadPool pool(4);
        
        // 提交计算任务并获取结果
        std::vector<std::future<long long>> compute_results;
        
        for (int i = 0; i < 5; ++i) {
            auto future = pool.enqueue(compute_task, i + 1, (i + 1) * 100000);
            compute_results.push_back(std::move(future));
        }
        
        // 提交字符串处理任务
        std::vector<std::future<std::string>> string_results;
        std::vector<std::string> inputs = {"hello", "world", "cpp", "thread", "pool"};
        
        for (size_t i = 0; i < inputs.size(); ++i) {
            auto future = pool.enqueue(string_task, inputs[i], 3);
            string_results.push_back(std::move(future));
        }
        
        // 收集计算结果
        std::cout << "\nCompute results:" << std::endl;
        long long total_sum = 0;
        for (size_t i = 0; i < compute_results.size(); ++i) {
            long long result = compute_results[i].get();
            total_sum += result;
            std::cout << "Task " << (i + 1) << " result: " << result << std::endl;
        }
        std::cout << "Total sum: " << total_sum << std::endl;
        
        // 收集字符串结果
        std::cout << "\nString results:" << std::endl;
        for (size_t i = 0; i < string_results.size(); ++i) {
            std::string result = string_results[i].get();
            std::cout << "String task " << (i + 1) << ": " << result << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error in return values test: " << e.what() << std::endl;
    }
}

// Lambda和现代C++特性测试
void test_modern_cpp_features() {
    std::cout << "\n=== 现代C++特性测试 ===" << std::endl;
    
    try {
        ThreadPool pool(6);
        
        // 使用Lambda表达式
        std::vector<std::future<int>> lambda_results;
        
        for (int i = 0; i < 10; ++i) {
            // 直接使用Lambda
            auto future = pool.enqueue([i](int multiplier) -> int {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                return i * multiplier;
            }, 10);
            lambda_results.push_back(std::move(future));
        }
        
        // 使用函数对象
        auto lambda_task = create_lambda_task(5);
        std::vector<std::future<int>> func_obj_results;
        
        for (int i = 0; i < 5; ++i) {
            auto future = pool.enqueue(lambda_task, i + 1);
            func_obj_results.push_back(std::move(future));
        }
        
        // 使用std::bind
        auto bound_task = std::bind(compute_task, std::placeholders::_1, 50000);
        std::vector<std::future<long long>> bound_results;
        
        for (int i = 0; i < 3; ++i) {
            auto future = pool.enqueue(bound_task, i + 100);
            bound_results.push_back(std::move(future));
        }
        
        // 收集Lambda结果
        std::cout << "Lambda results: ";
        for (auto& result : lambda_results) {
            std::cout << result.get() << " ";
        }
        std::cout << std::endl;
        
        // 收集函数对象结果
        std::cout << "Function object results: ";
        for (auto& result : func_obj_results) {
            std::cout << result.get() << " ";
        }
        std::cout << std::endl;
        
        // 收集绑定函数结果
        std::cout << "Bound function results: ";
        for (auto& result : bound_results) {
            std::cout << result.get() << " ";
        }
        std::cout << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in modern C++ features test: " << e.what() << std::endl;
    }
}

// 性能测试
void test_performance() {
    std::cout << "\n=== 性能测试 ===" << std::endl;
    
    const int thread_count = 8;
    const int task_count = 1000;
    
    try {
        ThreadPool pool(thread_count);
        std::cout << "Performance test: " << thread_count << " threads, " << task_count << " tasks" << std::endl;
        
        auto start_time = get_timestamp();
        
        // 提交大量轻量级任务
        std::vector<std::future<int>> results;
        results.reserve(task_count);
        
        for (int i = 0; i < task_count; ++i) {
            auto future = pool.enqueue([i]() -> int {
                // 模拟一些计算
                int sum = 0;
                for (int j = 0; j < 1000; ++j) {
                    sum += j;
                }
                return sum + i;
            });
            results.push_back(std::move(future));
        }
        
        std::cout << "All tasks submitted, waiting for completion..." << std::endl;
        
        // 等待所有任务完成并收集结果
        long long total = 0;
        for (auto& result : results) {
            total += result.get();
        }
        
        auto end_time = get_timestamp();
        
        std::cout << "Performance test completed in " << (end_time - start_time) << " ms" << std::endl;
        std::cout << "Total result: " << total << std::endl;
        std::cout << "Average time per task: " << (double)(end_time - start_time) / task_count << " ms" << std::endl;
        std::cout << "Completed tasks: " << pool.completed_task_count() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in performance test: " << e.what() << std::endl;
    }
}

// 异常处理测试
void test_exception_handling() {
    std::cout << "\n=== 异常处理测试 ===" << std::endl;
    
    try {
        ThreadPool pool(2);
        
        // 提交会抛出异常的任务
        auto future1 = pool.enqueue([]() -> int {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            throw std::runtime_error("Test exception");
            return 42;
        });
        
        // 提交正常任务
        auto future2 = pool.enqueue([]() -> int {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            return 100;
        });
        
        // 处理异常任务
        try {
            int result1 = future1.get();
            std::cout << "Unexpected result: " << result1 << std::endl;
        } catch (const std::exception& e) {
            std::cout << "✓ Caught expected exception: " << e.what() << std::endl;
        }
        
        // 处理正常任务
        int result2 = future2.get();
        std::cout << "✓ Normal task result: " << result2 << std::endl;
        
        // 测试无效参数
        try {
            ThreadPool invalid_pool(0);
            std::cout << "✗ Should have thrown exception for 0 threads" << std::endl;
        } catch (const std::invalid_argument& e) {
            std::cout << "✓ Correctly caught invalid argument: " << e.what() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error in exception handling test: " << e.what() << std::endl;
    }
}

// 关闭和等待测试
void test_shutdown_and_wait() {
    std::cout << "\n=== 关闭和等待测试 ===" << std::endl;
    
    try {
        ThreadPool pool(4);
        
        // 提交一些长时间运行的任务
        std::vector<std::future<void>> results;
        
        for (int i = 0; i < 8; ++i) {
            auto future = pool.enqueue([i]() {
                std::cout << "Long task " << i << " started" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                std::cout << "Long task " << i << " completed" << std::endl;
            });
            results.push_back(std::move(future));
        }
        
        std::cout << "Submitted 8 long-running tasks" << std::endl;
        std::cout << "Queue size: " << pool.queue_size() << std::endl;
        
        // 等待完成（带超时）
        std::cout << "Waiting for completion with 2 second timeout..." << std::endl;
        bool completed = pool.wait_for_completion(2000);
        
        if (completed) {
            std::cout << "✓ All tasks completed within timeout" << std::endl;
        } else {
            std::cout << "⚠ Timeout reached, some tasks may still be running" << std::endl;
            std::cout << "Active threads: " << pool.active_count() << std::endl;
            std::cout << "Queue size: " << pool.queue_size() << std::endl;
        }
        
        // 等待所有future完成
        for (auto& result : results) {
            result.wait();
        }
        
        std::cout << "All futures completed" << std::endl;
        std::cout << "Final completed task count: " << pool.completed_task_count() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in shutdown and wait test: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "面向对象的线程池测试程序" << std::endl;
    std::cout << "================================" << std::endl;
    std::cout << "Hardware concurrency: " << std::thread::hardware_concurrency() << " threads" << std::endl;
    
    test_basic_functionality();
    test_return_values();
    test_modern_cpp_features();
    test_performance();
    test_exception_handling();
    test_shutdown_and_wait();
    
    std::cout << "\n所有测试完成" << std::endl;
    
    return 0;
}