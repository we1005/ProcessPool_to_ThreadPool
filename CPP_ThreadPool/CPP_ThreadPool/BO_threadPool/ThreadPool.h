/**
 * Project 63th
 */

#ifndef _THREADPOOL_H
#define _THREADPOOL_H
#include "TaskQueue.h"
#include <iostream>
#include <vector>
#include <thread>
using std::cout;
using std::endl;
using std::vector;
using std::thread;


class ThreadPool {
public: 
    /* using ElemType = Task*; */
    /**
     * @param threadNum
     * @param queSize
     */
    ThreadPool(size_t threadNum, size_t queSize);
    ~ThreadPool();

    //启动线程池与关闭线程池
    void start();
    void stop();

    /**
     * @param ptask Task*
     */

    //添加任务
    void addTask(ElemType task);
    //获取任务
    ElemType getTask();
    //线程池交给工作线程执行的任务(线程入口函数)
    void doTask();
private: 
    size_t m_threadNum; //线程池中子线程的数量
    vector<thread> m_threads; //存放子线程的容器
    size_t m_queSize; //任务队列的大小
    TaskQueue m_taskQue; //任务队列
    bool m_isExit; //标志位，标志线程是否退出
};

#endif //_THREADPOOL_H
