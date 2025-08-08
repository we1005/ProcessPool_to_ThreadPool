#include "threadpool.hpp"

void ThreadPool::threadFunc()
{
    while (!isExit) 
    {
                ElementType task;
                {
                    std::unique_lock<mutex> lock(mtx);
                    _cond.wait(lock, [this]() { return isExit|| !TaskQue.empty(); });
                    if (isExit && TaskQue.empty()) return; // 即便检测到退出的标志位，也要保证任务全部做完再退出。
                    if (!TaskQue.empty()) {
                        task = std::move(TaskQue.front());
                        TaskQue.pop();
                    }
                }// 离开作用域时自动解锁（如果仍持有锁）
                if (task) {
                    task(); // Execute the task
                }
    }
}

ThreadPool::ThreadPool(int num_threads) : isExit(false), _thread_num(num_threads) 
{
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(threadFunc);
        threads[i].detach();
    }
}

ThreadPool::~ThreadPool() {
    isExit = true;
    notifyAll();
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}