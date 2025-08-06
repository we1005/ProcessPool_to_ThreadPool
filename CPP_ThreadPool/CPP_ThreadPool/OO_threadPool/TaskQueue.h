/**
 * Project 63th
 */

#ifndef _TASKQUEUE_H
#define _TASKQUEUE_H

#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>
using std::queue;
using std::mutex;
using std::unique_lock;
using std::condition_variable;

class Task;

using ElemType = Task*;

class TaskQueue {
public: 
    /**
     * @param capa
     */

    TaskQueue(size_t capa);
    ~TaskQueue();

    //判空与判满
    bool empty() const;
    bool full() const;

    /**
     * @param ptask
     */
    //添加任务与获取任务
    void push(ElemType ptask);
    ElemType pop();

    //唤醒所有阻塞的子线程
    void wakeup();

private: 
    size_t m_capacity;//任务队列的容量大小
    queue<ElemType> m_que; //存放任务的容器
    mutex m_mtx; //互斥锁
    condition_variable m_notEmpty;
    condition_variable m_notFull;

    //这个标志位设置的目的就是让m_notEmpty
    //条件变量上的线程可以跳出循环
    bool m_flag = true;
};

#endif //_TASKQUEUE_H
