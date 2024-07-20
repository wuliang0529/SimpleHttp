#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "Server.c"

int main(int argc, char* argv[]) {    //启动程序让用户制定命令行参数，argv[0]:可执行程序的名字, argv[1]:端口号, argv[2]:请求资源在服务器中的路径

    if(argc < 3) {   //如果参数小于3，则证明客户端给出的命令不对
        printf("Pleace iput: ./a.out port path\n");
        return -1;
    }
    unsigned short port = atoi(argv[1]);
    //切换服务器的工作路径（也就是客户端请求资源的根目录---并不是服务器的根目录哦！！）
    chdir(argv[2]);

    //初始化用于监听的套接字
    int lfd = initListenFd(port);    //端口尽量不要使用5000以下，防止冲突
    
    //启动服务器
    epollRun(lfd);

    return 0;
}