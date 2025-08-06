#include <63func.h>
// 1.0
//int recvFile(int sockfd){
//    char filename[4096] = {0};
//    recv(sockfd,filename,sizeof(filename),0);
//    int fd = open(filename,O_RDWR|O_CREAT|O_TRUNC,0666);
//    char buf[1000] = {0};
//    ssize_t sret = recv(sockfd,buf,sizeof(buf),0);
//    write(fd,buf,sret);
//    return 0;
//}
// 2.0
//typedef struct train_s {
//    int length;
//    char data[1000];
//} train_t;
//int recvFile(int sockfd){
//    train_t train;
//    // 先收小火车
//    recv(sockfd,&train.length,sizeof(train.length),0);//先收火车头
//    recv(sockfd,train.data,train.length,0);// 再收火车车厢
//    char filename[4096] = {0};
//    memcpy(filename,train.data,train.length);// 从车厢里面取出文件名
//    int fd = open(filename,O_RDWR|O_CREAT|O_TRUNC,0666);
//    recv(sockfd,&train.length,sizeof(train.length),0);//先收火车头
//    recv(sockfd,train.data,train.length,0);// 再收火车车厢
//    write(fd,train.data,train.length);
//    return 0;
//}
//3.0
//typedef struct train_s {
//    int length;
//    char data[1000];
//} train_t;
//int recvFile(int sockfd){
//    train_t train;
//    // 先收小火车
//    recv(sockfd,&train.length,sizeof(train.length),MSG_WAITALL);//先收火车头
//    recv(sockfd,train.data,train.length,MSG_WAITALL);// 再收火车车厢
//    char filename[4096] = {0};
//    memcpy(filename,train.data,train.length);// 从车厢里面取出文件名
//    int fd = open(filename,O_RDWR|O_CREAT|O_TRUNC,0666);
//    
//    while(1){
//        recv(sockfd,&train.length,sizeof(train.length),MSG_WAITALL);//先收火车头
//        if(train.length != 1000){
//            printf("train.length = %d\n", train.length);
//        }
//        if(train.length == 0){
//            break;
//        }
//        recv(sockfd,train.data,train.length,MSG_WAITALL);// 再收火车车厢
//        write(fd,train.data,train.length);
//    }
//    return 0;
//}
//3.1 自己实现类似于MSG_WAITALL的效果
//typedef struct train_s {
//    int length;
//    char data[1000];
//} train_t;
//int recvn(int sockfd, void *buf, int length){
//    char * p = (char *)buf;//强转成char *，方便偏移 
//    int total = 0;//total就是偏移量
//    while(total < length){//只要没有读够length
//        ssize_t sret = recv(sockfd, p+total, length-total, 0);
//        total += sret;
//    }
//    return length;
//}
//int recvFile(int sockfd){
//    train_t train;
//    // 先收小火车
//    recvn(sockfd,&train.length,sizeof(train.length));//先收火车头
//    recvn(sockfd,train.data,train.length);// 再收火车车厢
//    char filename[4096] = {0};
//    memcpy(filename,train.data,train.length);// 从车厢里面取出文件名
//    int fd = open(filename,O_RDWR|O_CREAT|O_TRUNC,0666);
//    while(1){
//        sleep(1);
//        recvn(sockfd,&train.length,sizeof(train.length));//先收火车头
//        if(train.length != 1000){
//            printf("train.length = %d\n", train.length);
//        }
//        if(train.length == 0){
//            break;
//        }
//        recvn(sockfd,train.data,train.length);// 再收火车车厢
//        write(fd,train.data,train.length);
//    }
//    return 0;
//}
//4.0 
typedef struct train_s {
    int length;
    char data[1000];
} train_t;
int recvn(int sockfd, void *buf, int length){
    char * p = (char *)buf;//强转成char *，方便偏移 
    int total = 0;//total就是偏移量
    while(total < length){//只要没有读够length
        ssize_t sret = recv(sockfd, p+total, length-total, 0);
        total += sret;
    }
    return length;
}
int recvFile(int sockfd){
    train_t train;
    // 先收小火车
    recvn(sockfd,&train.length,sizeof(train.length));//先收火车头
    recvn(sockfd,train.data,train.length);// 再收火车车厢
    char filename[4096] = {0};
    memcpy(filename,train.data,train.length);// 从车厢里面取出文件名
    int fd = open(filename,O_RDWR|O_CREAT|O_TRUNC,0666);
    
    off_t filesize;
    recvn(sockfd,&train.length,sizeof(train.length));//先收火车头
    recvn(sockfd,train.data,train.length);// 再收火车车厢
    memcpy(&filesize,train.data,train.length);// 从车厢里面取出文件长度
    printf("filesize = %ld\n", filesize);

    off_t current = 0;
    off_t lastsize = 0; // 上一次执行printf时的已发送长度
    off_t slice = filesize/10000; // 文件总大小的0.01%
    while(1){
        recvn(sockfd,&train.length,sizeof(train.length));//先收火车头
        sleep(1);
        if(train.length != 1000){
            printf("train.length = %d\n", train.length);
        }
        current += train.length; //把文件内容的火车头累加起来
        if(current - lastsize > slice){
            printf("%5.2lf%%\r", current * 100.0 /filesize);
            fflush(stdout); // 手动刷新缓冲区
            lastsize = current;
        }
        if(train.length == 0){
            break;
        }
        recvn(sockfd,train.data,train.length);// 再收火车车厢
        write(fd,train.data,train.length);
    }
    printf("100.00%%\n");
    return 0;
}
//5.0
//typedef struct train_s {
//    int length;
//    char data[1000];
//} train_t;
//int recvn(int sockfd, void *buf, int length){
//    char * p = (char *)buf;//强转成char *，方便偏移 
//    int total = 0;//total就是偏移量
//    while(total < length){//只要没有读够length
//        ssize_t sret = recv(sockfd, p+total, length-total, 0);
//        total += sret;
//    }
//    return length;
//}
//int recvFile(int sockfd){
//    train_t train;
//    // 先收小火车
//    recvn(sockfd,&train.length,sizeof(train.length));//先收火车头
//    recvn(sockfd,train.data,train.length);// 再收火车车厢
//    char filename[4096] = {0};
//    memcpy(filename,train.data,train.length);// 从车厢里面取出文件名
//    int fd = open(filename,O_RDWR|O_CREAT|O_TRUNC,0666);
//    
//    off_t filesize;
//    recvn(sockfd,&train.length,sizeof(train.length));//先收火车头
//    recvn(sockfd,train.data,train.length);// 再收火车车厢
//    memcpy(&filesize,train.data,train.length);// 从车厢里面取出文件长度
//    printf("filesize = %ld\n", filesize);
//
//    ftruncate(fd,filesize);
//    char *p = (char *)mmap(NULL,filesize,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
//    recvn(sockfd,p,filesize);
//    munmap(p,filesize);
//    return 0;
//}
int main(int argc, char *argv[])
{
    // ./client 192.168.72.128 12345
    ARGS_CHECK(argc,3);
    int sockfd = socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    addr.sin_addr.s_addr = inet_addr(argv[1]);
    int ret = connect(sockfd,(struct sockaddr *)&addr,sizeof(addr));
    ERROR_CHECK(ret,-1,"connect");
    recvFile(sockfd);
    close(sockfd);
    return 0;
}

