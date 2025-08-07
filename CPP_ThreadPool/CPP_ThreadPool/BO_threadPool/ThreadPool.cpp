/**
 * Project 63th
 */


#include "ThreadPool.h"

/**
 * ThreadPool implementation
 */


/**
 * @param threadNum
 * @param queSize
 */
ThreadPool::ThreadPool(size_t threadNum, size_t queSize) 
: m_threadNum(threadNum)
, m_threads()
, m_queSize(queSize)
, m_taskQue(m_queSize)
, m_isExit(false)
{}

ThreadPool::~ThreadPool() 
{}

/**
 * @return void
 */
//线程池启动
void ThreadPool::start() {
    //将子线程创建出来，存放到vector中
    for(size_t idx = 0; idx < m_threadNum; ++idx)
    {
        //创建线程的时候应该把线程入口函数传入
        //thread的拷贝构造已被删除，注意不能触发复制
        /* thread th(&ThreadPool::doTask,this); */
        /* m_threads.push_back(std::move(th)); */

        m_threads.push_back(thread(&ThreadPool::doTask,this));
    }
}

/**
 * @return void
 */
void ThreadPool::stop() {
    //如果任务队列不为空，就让主线程睡眠
    while(!m_taskQue.empty())
    {
        /* sleep(1); */
        //C++11提供的写法
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    //标志位设为true，表示线程池要退出了
    m_isExit = true;

    //回收子线程之前应该唤醒所有阻塞的子线程
    //
    //需要将ThreadPool声明为TaskQueue的友元
    /* m_taskQue.m_notEmpty.notify_all(); */
    m_taskQue.wakeup();


    //让主线程等待子线程的退出
    for(auto & th : m_threads)
    {
        th.join();
    }
}

/**
 * @param ptask Task*
 * @return void
 */

//添加任务到任务队列
void ThreadPool::addTask(ElemType task) {
    if(task)
    {
        //往任务队列中添加任务确实需要考虑上锁解锁
        //以及线程等待的问题
        //但此处的m_taskQue是一个实现好的TaskQueue对象
        //其push函数中已经封装好了所有的逻辑
        m_taskQue.push(task);
    }
}

/**
 * @return Task*
 */
//从任务队列中获取任务
ElemType ThreadPool::getTask() {
    return m_taskQue.pop();
}

/**
 * @return void
 */
//线程池交给子线程执行的任务
void ThreadPool::doTask() {
    //只要线程池没有退出，就应该让工作线程一直执行任务
    //另外，如果任务队列为空，应该让工作线程睡眠
    while(!m_isExit)
    {
        ElemType task = getTask();
        if(task)
        {
            //动态多态
            /* ptask->process(); */
            //回调
            task();
        }
        else
        {
            if(m_isExit) {
                break;  // 退出循环
            }
            else
            {
                cout << "ptask is a nullptr" << endl;
            }
        }
    }
}
