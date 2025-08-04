#include <header.h>

// 自己手动实现一个recvn函数
// 要求这个函数具有读取指定的size字节个数据, 没有读到, 就不要返回
int recvn(int net_fd, char *buf, int size){

    return 0;
}
int main(){

    char *port = "8080";
    char *ip = "192.168.106.130";

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in sockaddr;
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(atoi(port));
    sockaddr.sin_addr.s_addr = inet_addr(ip);

    connect(socket_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    

    // 信息在应用层有边界
    int file_name_len;
    recv(socket_fd, &file_name_len, sizeof(int), 0);

    char file_name[60] = {0};
    recv(socket_fd,file_name, file_name_len, 0 );

    int file_fd = open(file_name, O_RDWR|O_CREAT, 0600);

    // 接收文件大小
    off_t file_size = 0;
    recv(socket_fd, &file_size, sizeof(off_t), MSG_WAITALL);
    printf("client: file_size: %ld \n", file_size);

    // 首先在mmap之前调整文件大小, 让文件足够大
    ftruncate(file_fd, file_size);

    // 接收数据
    void *p = mmap(NULL, file_size, PROT_READ|PROT_WRITE, MAP_SHARED, file_fd, 0);

    recv(socket_fd, p, file_size, MSG_WAITALL);

    munmap(p, file_size);

    close(file_fd);
    close(socket_fd);
    return 0;
}

