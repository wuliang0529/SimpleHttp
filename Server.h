#pragma once

//初始化用于监听的套接字
int initListenFd(unsigned short port);     //监听的是文件描述符
//启动epoll
int epollRun(int lfd);
//和客户端建立连接
// int acceptClient(int lfd, int epfd);
void* acceptClient(void* arg);
//接收http请求
// int recvHttpRequest(int cfd, int epfd);
void* recvHttpRequest(void* arg);
//解析请求行~
int parseRequestLine(const char* line, int cfd);    //line是需要解析的字符串，cfd是给客户端通信的文件描述符
//发送文件
int sendFile(const char* filename, int cfd);
//发送响应头（包括状态行和响应头）
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length);
//根据文件名解析content-type
const char* getFileType(const char* name);  
//发送目录
int sendDir(const char* dirName, int cfd);
//将字符转化为整型数---辅助函数
int hexToDec(char c);
//解码函数---存在汉字或特殊字符时需要进行解码
void decodeMsg(char* to, char* from);
