#ifndef __Task_H__
#define __Task_H__
#include <iostream>
#include <mutex>
using std::cout;
using std::endl;
using std::mutex;
using std::lock_guard;

class Task
{
public:
    virtual void process() = 0;
    virtual ~Task(){}
};

class TaskA
: public Task
{
    virtual void process() override;
};




#endif

