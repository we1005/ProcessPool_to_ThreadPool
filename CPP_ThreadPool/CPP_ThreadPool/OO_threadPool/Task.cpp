#include "Task.h"
#include <iostream>
using std::cout;
using std::endl;

mutex coutMutex;

void TaskA::process() 
{
    srand(clock());
    int num = rand() % 100;
    {
        lock_guard<mutex> lg(coutMutex);
        cout << ">> TaskA num = " 
            << num << endl;
    }
}

