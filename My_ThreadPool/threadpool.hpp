#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP
#include <iostream>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include<atomic>
using std::atomic;
//基于C++11的线程池实现
using std::vector;
using std::thread;
using std::queue;
using std::mutex;
using std::condition_variable;
using std::function;

using ElementType = function<void()>;
class ThreadPool 
{
public:
    ThreadPool(int num_threads);
    ~ThreadPool();
    void addTask(ElementType task);
    //void wait();

private:
    vector<thread> threads;
    queue<ElementType> TaskQue;
    atomic<bool> isExit;
    mutex mtx;
    condition_variable _cond;
    //atomic<size_t> _task_num_now;// 当前任务数量
    atomic<size_t> _thread_num;// 线程数量
    void threadFunc();
    void notifyAll();
    void notifyOne();

};





#endif