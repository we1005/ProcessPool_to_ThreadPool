#include "ThreadPool.h"
#include <iostream>

ThreadPool::ThreadPool(size_t threads) 
    : stop(false), active_threads(0), completed_tasks(0) {
    
    if (threads == 0) {
        throw std::invalid_argument("Thread count must be greater than 0");
    }
    
    if (threads > std::thread::hardware_concurrency() * 2) {
        std::cerr << "Warning: Thread count (" << threads 
                  << ") exceeds recommended maximum (" 
                  << std::thread::hardware_concurrency() * 2 << ")" << std::endl;
    }
    
    // 创建工作线程
    workers.reserve(threads);
    try {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back(&ThreadPool::worker_thread, this);
        }
    } catch (...) {
        // 如果创建线程失败，清理已创建的线程
        stop = true;
        condition.notify_all();
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        throw;
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (stop) {
            return; // 已经关闭
        }
        stop = true;
    }
    
    // 通知所有工作线程
    condition.notify_all();
    
    // 等待所有线程完成
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::shutdown_now() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (stop) {
            return; // 已经关闭
        }
        stop = true;
        
        // 清空任务队列
        std::queue<std::function<void()>> empty;
        tasks.swap(empty);
    }
    
    // 通知所有工作线程
    condition.notify_all();
    
    // 等待所有线程完成
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

bool ThreadPool::wait_for_completion(int timeout_ms) {
    std::unique_lock<std::mutex> lock(queue_mutex);
    
    if (timeout_ms <= 0) {
        // 无限等待
        finished.wait(lock, [this] {
            return tasks.empty() && active_threads == 0;
        });
        return true;
    } else {
        // 有超时的等待
        return finished.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
            return tasks.empty() && active_threads == 0;
        });
    }
}

size_t ThreadPool::size() const noexcept {
    return workers.size();
}

size_t ThreadPool::active_count() const noexcept {
    return active_threads.load();
}

size_t ThreadPool::queue_size() const noexcept {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return tasks.size();
}

bool ThreadPool::is_shutdown() const noexcept {
    return stop.load();
}

size_t ThreadPool::completed_task_count() const noexcept {
    return completed_tasks.load();
}

void ThreadPool::worker_thread() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            // 等待任务或停止信号
            condition.wait(lock, [this] {
                return stop || !tasks.empty();
            });
            
            // 如果停止且没有任务，退出
            if (stop && tasks.empty()) {
                return;
            }
            
            // 取出任务
            if (!tasks.empty()) {
                task = std::move(tasks.front());
                tasks.pop();
                active_threads++;
            }
        }
        
        // 执行任务
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "Task execution failed: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Task execution failed with unknown exception" << std::endl;
            }
            
            // 任务完成
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                active_threads--;
                completed_tasks++;
                
                // 如果所有任务都完成了，通知等待的线程
                if (tasks.empty() && active_threads == 0) {
                    finished.notify_all();
                }
            }
        }
    }
}