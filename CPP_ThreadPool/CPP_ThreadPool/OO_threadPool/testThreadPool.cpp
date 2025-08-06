#include "ThreadPool.h"
#include <memory>
#include <iostream>
using std::cout;
using std::endl;
using std::unique_ptr;

void test0()
{
    unique_ptr<Task> ptask(new TaskA());
    ThreadPool pool(5,10);
    pool.start();

    int count = 20;
    while(count--)
    {
        //问题1.确实往任务队列中加入过20个任务
        //但是执行的任务数量不满20个
        //程序就退出了 —— doTask执行的次数不够
        pool.addTask(ptask.get());
        /* cout << count << endl; */
    }
    
    pool.stop();
}

int main(void)
{
    test0();
    return 0;
}

