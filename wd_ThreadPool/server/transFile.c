#include "threadPool.h"
// 1.0
//int transFile(int netfd){
//    char filename[] = "file1";
//    send(netfd,filename,strlen(filename),0);
//    int fd = open(filename,O_RDWR);
//    char buf[1000] = {0};//用char数组不是因为数据一定是字符串，只是为了方便控制申请内存的大小
//    ssize_t sret = read(fd,buf,sizeof(buf));
//    send(netfd,buf,sret,0);// sret是read的返回值，无论是文本文件还是二进制文件都能准确获取长度
//    return 0;
//}
// 2.0 使用小火车发小文件
//typedef struct train_s {
//    int length; //火车头 固定4Bytes
//    char data[1000]; // 火车车厢 上限是1000Bytes
//} train_t;
//int transFile(int netfd){
//    train_t train; // 在栈上申请一个小火车
//    char filename[] = "file1";
//    train.length  = strlen(filename);//先填火车头
//    memcpy(train.data,filename,train.length);//填充火车车厢
//    send(netfd,&train,sizeof(train.length)+train.length, 0);//火车的实际长度 = 火车头长度+火车车厢长度
//
//    int fd = open(filename,O_RDWR);
//    char buf[1000] = {0};//用char数组不是因为数据一定是字符串，只是为了方便控制申请内存的大小
//    ssize_t sret = read(fd,buf,sizeof(buf));
//    train.length = sret;
//    memcpy(train.data,buf,train.length);
//    send(netfd,&train,sizeof(train.length)+train.length, 0);
//    return 0;
//}
//3.0 使用小火车发大文件
//typedef struct train_s {
//    int length; //火车头 固定4Bytes
//    char data[1000]; // 火车车厢 上限是1000Bytes
//} train_t;
//int transFile(int netfd){
//    train_t train; // 在栈上申请一个小火车
//    char filename[] = "file1";
//    train.length  = strlen(filename);//先填火车头
//    memcpy(train.data,filename,train.length);//填充火车车厢
//    send(netfd,&train,sizeof(train.length)+train.length, MSG_NOSIGNAL);//火车的实际长度 = 火车头长度+火车车厢长度
//
//    int fd = open(filename,O_RDWR);
//    while(1){
//        ssize_t sret = read(fd,train.data,sizeof(train.data));
//        train.length = sret;
//        if(train.length != 1000 && train.length != -1){
//            printf("train.length = %d\n", train.length);
//        }
//        send(netfd,&train,sizeof(train.length)+train.length, MSG_NOSIGNAL);
//        // 当文件没有剩余内容时，read返回0，先装一个空的火车头，发出去以后，再退
//        if(sret == 0){
//            break;
//        }
//    }
//    return 0;
//}
//4.0 加进度条显示
typedef struct train_s {
    int length; //火车头 固定4Bytes
    char data[1000]; // 火车车厢 上限是1000Bytes
} train_t;
int transFile(int netfd){
    train_t train; // 在栈上申请一个小火车
    char filename[] = "file1";
    train.length  = strlen(filename);//先填火车头
    memcpy(train.data,filename,train.length);//填充火车车厢
    send(netfd,&train,sizeof(train.length)+train.length, MSG_NOSIGNAL);//火车的实际长度 = 火车头长度+火车车厢长度

    int fd = open(filename,O_RDWR);
    struct stat statbuf;
    fstat(fd,&statbuf);
    off_t filesize = statbuf.st_size;
    train.length = sizeof(filesize);
    memcpy(train.data,&filesize,train.length);
    send(netfd,&train,sizeof(train.length)+train.length,MSG_NOSIGNAL);

    while(1){
        ssize_t sret = read(fd,train.data,sizeof(train.data));
        train.length = sret;
        if(train.length != 1000 && train.length != -1){
            printf("train.length = %d\n", train.length);
        }
        send(netfd,&train,sizeof(train.length)+train.length, MSG_NOSIGNAL);
        // 当文件没有剩余内容时，read返回0，先装一个空的火车头，发出去以后，再退
        if(sret == 0){
            break;
        }
    }
    return 0;
}
//4.1 将文件用mmap映射
//typedef struct train_s {
//    int length; //火车头 固定4Bytes
//    char data[1000]; // 火车车厢 上限是1000Bytes
//} train_t;
//int transFile(int netfd){
//    train_t train; // 在栈上申请一个小火车
//    char filename[] = "file1";
//    train.length  = strlen(filename);//先填火车头
//    memcpy(train.data,filename,train.length);//填充火车车厢
//    send(netfd,&train,sizeof(train.length)+train.length, MSG_NOSIGNAL);//火车的实际长度 = 火车头长度+火车车厢长度
//
//    int fd = open(filename,O_RDWR);
//    struct stat statbuf;
//    fstat(fd,&statbuf);
//    off_t filesize = statbuf.st_size;
//    train.length = sizeof(filesize);
//    memcpy(train.data,&filesize,train.length);
//    send(netfd,&train,sizeof(train.length)+train.length,MSG_NOSIGNAL);
//
//    ftruncate(fd,filesize);//固定文件的大小
//    char *p = (char *)mmap(NULL,filesize,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
//    off_t current = 0; //已经发送的长度
//    while(1){
//        if(current >= filesize){
//            break;
//        }
//        if(filesize - current >= 1000){
//            // 剩下的内容一个火车装不下
//            train.length = 1000; // 火车头是1000
//        }
//        else{
//            train.length = filesize - current;
//        }
//        send(netfd,&train.length,sizeof(train.length),MSG_NOSIGNAL); //先发火车头
//        send(netfd,p+current,train.length, MSG_NOSIGNAL); //再发车厢 
//        current += train.length; //current后移
//    }
//    train.length = 0;
//    send(netfd,&train.length,sizeof(train.length),MSG_NOSIGNAL); 
//    munmap(p,filesize);
//    return 0;
//}
//5.0 一次性发送
//typedef struct train_s {
//    int length; //火车头 固定4Bytes
//    char data[1000]; // 火车车厢 上限是1000Bytes
//} train_t;
//int transFile(int netfd){
//    train_t train; // 在栈上申请一个小火车
//    char filename[] = "file1";
//    train.length  = strlen(filename);//先填火车头
//    memcpy(train.data,filename,train.length);//填充火车车厢
//    send(netfd,&train,sizeof(train.length)+train.length, MSG_NOSIGNAL);//火车的实际长度 = 火车头长度+火车车厢长度
//
//    int fd = open(filename,O_RDWR);
//    struct stat statbuf;
//    fstat(fd,&statbuf);
//    off_t filesize = statbuf.st_size;
//    train.length = sizeof(filesize);
//    memcpy(train.data,&filesize,train.length);
//    send(netfd,&train,sizeof(train.length)+train.length,MSG_NOSIGNAL);
//
//    //ftruncate(fd,filesize);//固定文件的大小
//    //char *p = (char *)mmap(NULL,filesize,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
//    //send(netfd,p,filesize,MSG_NOSIGNAL);
//    //munmap(p,filesize);
//    sendfile(netfd,fd,NULL,filesize);
//    return 0;
//}
