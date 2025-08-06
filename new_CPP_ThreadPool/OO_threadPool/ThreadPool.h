#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>
#include <chrono>

/**
 * 面向对象的线程池类
 * 支持任意函数类型和返回值，使用现代C++特性
 */
class ThreadPool {
public:
    /**
     * 构造函数
     * @param threads 线程数量
     * @throws std::invalid_argument 如果线程数量无效
     */
    explicit ThreadPool(size_t threads);
    
    /**
     * 析构函数
     * 自动关闭线程池并等待所有任务完成
     */
    ~ThreadPool();
    
    /**
     * 禁用拷贝构造和赋值操作
     */
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
    /**
     * 向线程池提交任务
     * @param f 要执行的函数
     * @param args 函数参数
     * @return std::future对象，可用于获取任务结果
     * @throws std::runtime_error 如果线程池已关闭
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;
    
    /**
     * 关闭线程池
     * 不再接受新任务，但会等待现有任务完成
     */
    void shutdown();
    
    /**
     * 立即关闭线程池
     * 不再接受新任务，并尝试中断正在执行的任务
     */
    void shutdown_now();
    
    /**
     * 等待所有任务完成
     * @param timeout_ms 超时时间（毫秒），0表示无限等待
     * @return true如果所有任务完成，false如果超时
     */
    bool wait_for_completion(int timeout_ms = 0);
    
    /**
     * 获取线程池大小
     * @return 线程数量
     */
    size_t size() const noexcept;
    
    /**
     * 获取活跃线程数量
     * @return 当前正在执行任务的线程数量
     */
    size_t active_count() const noexcept;
    
    /**
     * 获取队列中等待的任务数量
     * @return 等待执行的任务数量
     */
    size_t queue_size() const noexcept;
    
    /**
     * 检查线程池是否已关闭
     * @return true如果已关闭
     */
    bool is_shutdown() const noexcept;
    
    /**
     * 获取已完成的任务总数
     * @return 完成的任务数量
     */
    size_t completed_task_count() const noexcept;
    
private:
    // 工作线程容器
    std::vector<std::thread> workers;
    
    // 任务队列
    std::queue<std::function<void()>> tasks;
    
    // 同步原语
    mutable std::mutex queue_mutex;
    std::condition_variable condition;
    std::condition_variable finished;
    
    // 状态变量
    std::atomic<bool> stop;
    std::atomic<size_t> active_threads;
    std::atomic<size_t> completed_tasks;
    
    /**
     * 工作线程函数
     */
    void worker_thread();
};

// 模板函数实现必须在头文件中
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    
    using return_type = typename std::result_of<F(Args...)>::type;
    
    // 创建packaged_task
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        
        // 检查线程池是否已停止
        if (stop) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        
        // 将任务添加到队列
        tasks.emplace([task](){ (*task)(); });
    }
    
    // 通知一个等待的线程
    condition.notify_one();
    return res;
}

#endif // THREADPOOL_H