#include "TaskQueue.h"

TaskQueue::TaskQueue(size_t cap)
    : m_capacity(cap)
{}

TaskQueue::~TaskQueue(){}

//判空和判满
bool TaskQueue::empty() const
{
    return m_que.size() == 0;
}

bool TaskQueue::full() const
{
    return m_que.size() == m_capacity;
}

//push就是往仓库中加数据
//应该是生产者会使用push函数
void TaskQueue::push(ElemType task)
{
    //1.先上锁
    /* m_mtx.lock(); */
    unique_lock<mutex> ul(m_mtx);

    //2.判断仓库是否已满
    /* if(full()) */
    while(full()) //以防虚假唤醒
    {
        //2.1
        //如果仓库是满的
        //就让生产者线程在m_notFull这个条件变量上等待
        m_notFull.wait(ul);//wait函数必须传入unique_lock

        //底层的效果实际上会让生产者线程阻塞，
        //并且释放锁
        //等到被唤醒时会重新拿到锁
    }

    //2.2
    //如果仓库不是满的
    //就可以让生产者生产数据，加入仓库
    m_que.push(task);

    //同时还应该唤醒消费者线程
    m_notEmpty.notify_one();

    //3.局部作用域结束时，unique_lock对象销毁
    //会自动解锁
}

//消费者线程会使用pop函数
//删除队列中首个元素，并且还要返回这个元素
ElemType TaskQueue::pop()
{
    //还是先上锁
    unique_lock<mutex> ul(m_mtx);
    while(empty() && m_flag)
    {
        //如果仓库是空的
        //就让消费者等待
        m_notEmpty.wait(ul);
    }

    //最后线程池退出时，才会让m_flag被置为false
    if(m_flag)
    {
        //仓库不是空的，可以让消费者消费数据
        ElemType temp = m_que.front();
        m_que.pop();

        //还应该唤醒生产者线程
        m_notFull.notify_one();
        return temp;
    }
    else
    {
        return function<void()>();
    }
}

void TaskQueue::wakeup()
{
    m_flag = false;//表示线程池要退出了

    m_notEmpty.notify_all();
}


