#include "ThreadPool.h"
#include <memory>
#include <iostream>
using std::cout;
using std::endl;
using std::unique_ptr;

class MyTask
{
public:
    void process(int x)
    {
        srand(clock());
        int num = rand() % 100;
        cout << ">>> BO_threadPool num = "
            << num << endl;
    }
};


void test0()
{
    unique_ptr<MyTask> ptask(new MyTask());
    ThreadPool pool(5,10);
    pool.start();

    int count = 20;
    while(count--)
    {
        //通过bind改变可调用实体的调用形态
        //比如此处将MyTask的process函数提供出来
        //打包成任务，添加到任务队列
        pool.addTask(bind(&MyTask::process,ptask.get(),100));
        /* cout << count << endl; */
    }
    
    pool.stop();
}

int main(void)
{
    test0();
    return 0;
}

