#include "head.h"

typedef struct train_s{
    int len;
    char buf[1000];
} train_t;

// 参数: 用来和指定客户端通信的socket的文件描述符
int sendFile(int net_fd){


    train_t file_name;
    bzero(&file_name, sizeof(file_name));

    char *str = "1b.txt";
    memcpy(file_name.buf, str, strlen(str));
    file_name.len = strlen(str);
    send(net_fd, &file_name, sizeof(int)+file_name.len, 0);

    // 发送文件内容
    int file_fd = open(str, O_RDWR);

    // 获取文件大小
    struct stat st;
    memset(&st, 0, sizeof(st));
    fstat(file_fd, &st);
    //printf("file_size: %ld \n", st.st_size);
    // 把文件大小发送给客户端
    send(net_fd, &st.st_size, sizeof(off_t), MSG_NOSIGNAL);


    // 使用sendfile: 零拷贝
    sendfile(net_fd, file_fd, NULL, st.st_size);
    
    close(file_fd);
    return 0;
}


