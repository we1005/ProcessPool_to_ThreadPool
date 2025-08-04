#include <header.h>

// 标识一个进程的状态
enum {
   BUSY,
   FREE
};
//定义一个结构体: 用来让父进程记录子进程的一些信息
typedef  struct son_status_s{
    pid_t pid;// 记录子进程的id
    int flag ; // 记录子进程的状态: 忙, 闲
    int local_socket;// 用来和子进程进行通信的本地socket

} son_status_t;

// 这是一个初始化进程池中进程的函数
// 第一个参数: 存储初始化进行的信息
// 第二个参数: 初始化几个进程
// 返回值: 0代表...-1....1...
int initPool(son_status_t *list, int num);

// 这是一个初始化socket服务端的函数
// 第一个参数: 传入传出, socket对象的文件描述符
// 第二个参数: 端口
// 第三个参数: ip
// 返回值...
int initSocket(int *socket_fd, char *port, char *ip);

// 把要监听的文件描述符放入epoll监听
// 第一个参数: epoll对象的文件描述符
// 第二个参数: 要放入监听的文件描述符
// 返回值
int addEpoll(int epoll_fd, int fd);

// 要求在子进程中, 选取一个空闲进程, 处理客户端链接
// 第一个参数: 父进程记录子进程信息
// 第二个参数: 子进程的个数
// 第三个参数: 新的socket对象(新的客户端链接)
int toSonNetFd(son_status_t *list, int num, int net_fd);

// 子进程的核心执行函数: 一直循环, 一直recvmsg读取任务
// 参数: 用来和父进程通信的本地socket的文件描述
int doWorker(int local_socket);

// 通过本地socket向对应的子进程发型一个net_fd相关的信息
// 第一个参数: 父子通信的本地socket
// 第二个参数: 父进程给子进程的文件描述符
// 第三个参数: 用来让父进程告诉子进程是否是要求退出: -1要求退出, 0:此退出标记无含义
int sendMsg(int local_socket, int net_fd, int flag);


// 子进程读取父进程通过本地socket发过来新的客户端的socket对象
// 第一个参数: 用来和父进程进程通信的本地socket
// 第二个参数: 接收从父进程传过来的用来和客户端交互的socket对象的文件描述符
// 第三个参数: 父进程给我子进程发送过来的退出标记:  0:不退出仅填充无意义,  -1:要求我退出
int recvMsg(int local_socket, int *net_fd, int *flag);


// 用来和指定的客户端交互
int toClientFile(int net_fd);

// 给客户端回传文件
// 参数: 用来和指定客户端通信的socket的文件描述符
int sendFile(int net_fd);
