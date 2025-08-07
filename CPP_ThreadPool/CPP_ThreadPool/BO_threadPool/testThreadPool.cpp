#include "ThreadPool.h"
#include <memory>
#include <iostream>
#include <mutex>
using std::cout;
using std::endl;
using std::unique_ptr;
using std::shared_ptr;
using std::mutex;
using std::lock_guard;

// 全局互斥锁用于保护cout输出
static mutex cout_mutex;

class MyTask
{
public:
    void process(int x)
    {
        srand(clock());
        int num = rand() % 100;
        
        // 使用互斥锁保护cout输出，避免多线程输出混乱
        {
            lock_guard<mutex> lock(cout_mutex);
            cout << ">>> BO_threadPool num = "
                << num << endl;
        }
    }
};


void test0()
{
    ThreadPool pool(5,10);
    pool.start();

    int count = 20;
    while(count--)
    {
        // 为每个任务创建新的MyTask对象
        // 使用shared_ptr确保对象在任务执行期间不会被销毁
        auto ptask = std::make_shared<MyTask>();
        
        //通过bind改变可调用实体的调用形态
        //比如此处将MyTask的process函数提供出来
        //打包成任务，添加到任务队列
        pool.addTask(bind(&MyTask::process, ptask.get(), 100));
        /* cout << count << endl; */
    }
    
    pool.stop();
}

int main(void)
{
    test0();
    return 0;
}

