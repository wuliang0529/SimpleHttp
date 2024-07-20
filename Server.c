#include "Server.h"
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/sendfile.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <pthread.h>

struct FdInfo {      //用于封装创建子线程时传入的两个参数（创建子线程只有一个参数传入位置，所以封装成结构体）
    int fd;
    int epfd;
    int tid;
};

int initListenFd(unsigned short port)
{
    // 1. 设置监听的fd
    int lfd = socket(AF_INET, SOCK_STREAM, 0);   //IPv4、流式协议（0：TCP）
    if(lfd == -1) {
        perror("socket");
        return -1;
    }
    // 2. 设置端口复用
    int opt = 1;
    int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt); 
    if(lfd == -1) {
        perror("setsoclopt");
        return -1;
    }
    // 3. 绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;     //使用的是IPv4格式的ip地址     AF_INET6: ipv6格式的ip地址 
    addr.sin_port = htons(port);         //转化为网络传输端口
    addr.sin_addr.s_addr = INADDR_ANY;                //要绑定的本地IP地址---指定为0，宏为INADDR_ANY
    ret = bind(lfd, (struct sockaddr*)&addr, sizeof addr);     //将sockaddr_in结构体类型强制转换为sockaddr结构体类型（绑定要求）
    if(ret == -1) {
        perror("bind");
        return -1;
    }
    // 4. 设置监听
    ret = listen(lfd, 128);    //一次性最多检测128个客户端请求（内核中设置最大值为128，超过部分不算）
    if(ret == -1) {
        perror("listern");
        return -1;
    }
    // 5. 返回fd
    return lfd;
}

int epollRun(int lfd)
{
    //1. 创建epoll实例---实现原理是红黑树
    int epfd = epoll_create(1);   //参数已被弃用，设置大于0的数即可，无实际意义
    if(epfd == -1) {
        perror("epoll_creat");
        return -1;
    }
    //2. lfd添加到epoll树--lfd上树
    struct epoll_event ev;
    ev.data.fd = lfd;
    ev.events = EPOLLIN;
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if(ret == -1) {
        perror("epoll_ctl");
        return -1;
    }

    //3. 检测是否有文件描述符被激活
    struct epoll_event evs[1024];
    int size = sizeof(evs) / sizeof(struct epoll_event);  //计算结构体大小
    while (1)
    {
        //实例化结构体
        struct FdInfo* info = (struct FdInfo*)malloc(sizeof(struct FdInfo));
        int num = epoll_wait(epfd, evs, size, -1);  //设置-1是如果树中没有文件描述符被激活，则处于阻塞，直至有被激活的事件发生
        for(int i=0; i<num; ++i) {
            int fd = evs[i].data.fd;
            //初始化结构体成员
            info->fd = fd;
            info->epfd = epfd;
            //info->tid在线程穿件的时候写入，因为创建函数的第一个参数为写出参数，直接传入结构体地址即可
            if(fd == lfd) {    //如果是链接文件描述符
                //建立新连接，调用accept，注意这个调用并不会被阻塞，因为已经确定了需要建立连接
                // acceptClient(lfd, epfd);
                pthread_create(&info->tid, NULL, acceptClient, info);
            }
            else{   //如果是通信文件描述符
                //主要处理接收对端的数据（写数据不作考虑）
                // recvHttpRequest(fd, epfd);
                pthread_create(&info->tid, NULL, recvHttpRequest, info);
            }
        }
    }
    
    return 0;
}

// int acceptClient(int lfd, int epfd)
void* acceptClient(void* arg)
{
    //把arg转换成struct FdInfo* 类型
    struct FdInfo* info = (struct FdInfo*)arg;
    //1. 建立连接
    int cfd = accept(info->fd, NULL, NULL);
    if(cfd == -1) {
        perror("accept");
        return NULL;
    }
    //2. 设置连接描述符的非阻塞属性
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;         //有效位设置为1
    fcntl(cfd, F_SETFL, flag);
    //3. 将文件描述符cfd添加到epoll树中
    struct epoll_event ev;
    ev.data.fd = cfd;
    ev.events = EPOLLIN | EPOLLET;   //检测文件描述符的读事件  ，EPOLLET设置epoll的边缘出发（默认是水平触发） 
    int ret = epoll_ctl(info->epfd, EPOLL_CTL_ADD, cfd, &ev);
    if(ret == -1) {
        perror("epoll_ctl");
        return NULL;
    }
    printf("acceptClient threadID: %ld\n", info->tid);   //打印线程ID，验证多线程,,,也可以使用pthread_self获取线程ID
    free(info);   //释放资源：fd和epfd均不需要关闭
    return NULL;
}

// int recvHttpRequest(int cfd, int epfd)
void* recvHttpRequest(void* arg)
{
    //把arg转换成struct FdInfo* 类型
    struct FdInfo* info = (struct FdInfo*) arg;
    int len = 0, totle = 0;
    char tmp[1024] = { 0 };   //读取缓冲
    char buf[4096] = { 0 };   //存储读取的数据
    while((len = recv(info->fd, tmp, sizeof tmp, 0))> 0) {
        if(totle + len < sizeof buf) {
            //memcy()函数功能：从源头指向的内存块拷贝固定字节数的数据到目标指向的内存块。
            memcpy(buf + totle, tmp, len);    //目的地(拷贝到这)的内存块起始地址:buf+totle, 源（要拷贝的数据源）内存块起始地址:tmp, 字节数：len
        }
        totle += len;
    }
    //判断数据是否被接受完毕(recv(cfd, tmp, sizeof tmp, 0)):读取错误或者读取完毕都会返回-1
    if(len == -1 && errno == EAGAIN){   //如果是读取完毕返回-1
        //解析请求行
        char* pt = strstr(buf, "\r\n");   //搜索字符串的换行位置---找到请求行，然后解析
        int reqlen = pt - buf;
        buf[reqlen] = '\0';   //截断字符串，读取字符串的时候默认读取到 \0 就会结束
        parseRequestLine(buf, info->fd);    //调用解析函数
    }
    else if(len == 0) {   //证明客户端已经断开连接
        epoll_ctl(info->epfd, EPOLL_CTL_DEL, info->fd, NULL);   //从epoll树中删除文件描述符cfd
        close(info->fd);    //关闭文件描述符
    }
    else{
        perror("recv");    //证明发生了其他错误
    }
    printf("recvMsg threadID: %ld\n", info->tid);   //打印线程ID，验证多线程,,,也可以使用pthread_self获取线程ID
    free(info);    //释放资源：fd需要关闭，epfd不需要关闭
    return 0;
}

int parseRequestLine(const char *line, int cfd)
{
    //解析请求：get /xxx/1.jpg http/1.1

    //使用sscanf拆解请求行的三部分---具体使用规则自行学习
    char method[12] = { 0 };   //请求方式
    char path[1024] = { 0 };   //请求的资源的根目录---并不是服务器的根目录
    sscanf(line, "%[^ ] %[^ ]", method, path);
    // printf("method: %s, path: %s", method, path);
    if(strcasecmp(method, "get") != 0) {    //判断是不是get请求，我们不处理post请求。 注意：strcasecmp比较函数不区分大小写
        return -1; 
    }
    decodeMsg(path, path);    //解码---中文或者特殊字符
    //处理客户端请求的静态资源（目录或者文件）
    char* file = NULL;
    if(strcasecmp(path, "/") == 0) {    //如果访问的是根目录
        file = "./";       //（因为服务器端已经切换到了客户端需要访问资源的根目录, 所以服务器当前目录即为客户端需要访问资源的根目录）
    }
    else{   //否则查找相对路径
        file = path + 1;    //注意：path也是个指针哦！这里是去掉path中最开始的 "/"
    }

    //获取文件的属性(结构体类型，比如包括文件类型，大小等等)
    struct stat st;
    int ret = stat(file, &st);
    if(ret == -1) {
        //文件不存在，回复客户端404页面...
        sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);
        sendFile("404.html", cfd);
        return 0;
    }
    if(S_ISDIR(st.st_mode)) {   //如果是目录（函数返回值为1证明为目录）
        //把这个目录中的内容发送给客户端
        sendHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
        sendDir(file, cfd);
    }
    else{
        //把文件的内容发送给客户端
        sendHeadMsg(cfd, 200, "OK", getFileType(file), st.st_size);
        sendFile(file, cfd);
    }

    return 0;
}

const char* getFileType(const char* name){

    //判断的类型如：a.jpg、a.mp4、a.html
    //从左向右找“.”，找到之后从左向右就可以得到完整的后缀名
    const char* dot = strrchr(name, '.');    //strrchr是从右向左找
    if(dot == NULL) {
        return "text/plain; charset=utf-8";   //纯文本
    }
    if(strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0){
        return "text/html; charset=utf-8";
    }
    if(strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0){
        return "image/jpeg";
    }
    if(strcmp(dot, ".gif") == 0){
        return "image/gif";
    }
    if(strcmp(dot, ".png") == 0){
        return "image/png";
    }
    if(strcmp(dot, ".css") == 0){
        return "text/css";
    }
    if(strcmp(dot, ".au") == 0){
        return "audio/basic";
    }
    if(strcmp(dot, ".wav") == 0){
        return "audio/wav";
    }
    if(strcmp(dot, ".avi") == 0){
        return "video/x-msvideo";
    }
    if(strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0){
        return "video/quicktime";
    }
    if(strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0){
        return "video/mpeg";
    }
    if(strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0){
        return "model/vrml";
    }
    if(strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0){
        return "audio/quicktime";
    }
    if(strcmp(dot, ".mp3") == 0){
        return "audio/mpeg";
    }
    if(strcmp(dot, ".mp4") == 0) {
        return "video/mp4";
    }
    if(strcmp(dot, ".ogg") == 0) {
        return "application/ogg";
    }
    if(strcmp(dot, ".pac") == 0) {
        return "application/x-ns-proxy-autoconfig";
    }
    return "text/plain; charset=utf-8";
}
/**html网页结构
 * <html>
 *      <head>
 *          <title>name</title>
 *          <meta http-equiv="content-type" content="text/html; charset=UTF-8">
 *      </head>
 *      <body>
 *          <table>
 *              <tr>
 *                  <td>内容</td>
 *              </tr>
 *              <tr>
 *                  <td>内容</td>
 *              </tr>
 *          </table> 
 *      </body>
 * </html>
 */

int sendDir(const char *dirName, int cfd)
{
    char* buf[4096] = { 0 };
    sprintf(buf, "<html><head><title>%s</title><meta charset=\"%s\"></head><body><table>", dirName, "UTF-8"); 
    struct dirent** namelist;
    int num = scandir(dirName, &namelist, NULL, alphasort);
    for(int i=0; i<num; ++i) {
        //取出文件名namelist，指向的是一个指针数组struct diren* tmp[]
        char* name = namelist[i]->d_name;    //取出来的是相对于dirName的相对路径，需要拼接一下
        struct stat st;
        char subPath[1024] = { 0 };
        sprintf(subPath, "%s/%s", dirName, name);
        stat(subPath, &st);   //用于判断subPath是文件还是目录
        if(S_ISDIR(st.st_mode)) {
            sprintf(buf + strlen(buf), "<tr><td><a href=\"%s/\">%s</a></td>%ld<td></td></tr>",name, name, st.st_size);
        }
        else{
            sprintf(buf + strlen(buf), "<tr><td><a href=\"%s\">%s</a></td>%ld<td></td></tr>",name, name, st.st_size);
        }
        send(cfd, buf, strlen(buf), 0);
        memset(buf, 0, sizeof(buf));
        free(namelist[i]);   //释放一级指针
    }
    sprintf(buf, "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);
    free(namelist);   //释放一下二级指针
    return 0;
}

int hexToDec(char c)
{
    if(c >= '0' && c <= '9') {
        return c - '0';
    }
    if(c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if(c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

//to存储解码之后的数据，传出参数，from被解码的数据，传入参数
void decodeMsg(char *to, char *from)    //字符串指针
{
    //Linux%E5%86%85%E6%A0%B8.jpg
    for(; *from != '\0'; ++to, ++from) {
        //isxdigit判断是不是16进制格式，取值在0-f
        if(from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])){
            //16进制  --->  10进制int   ----> 字符型char （整型到字符型存在隐式的自动类型转换）
            *to = hexToDec(from[1])*16 + hexToDec(from[2]);
            from += 2;
        }
        else{
            //拷贝字符，复制
            *to = *from;
        }
    }
    *to = '\0';    //字符串结尾
}

int sendFile(const char *filename, int cfd)
{
    
    //1.打开文件
    int fd = open(filename, O_RDONLY);   //只读的方式打开文件
    assert(fd > 0);   //断言判断文件是否打开成功
    #if 0
    while(1){
        char buf[1024] = { 0 };
        int len = read(fd, buf, sizeof buf);
        if(len > 0) {
            send(cfd, buf, len, 0);
            usleep(10);    //发送端休眠10微秒，保接收端接收到的数据可以及时处理
        }
        else if(len == 0) {
            break;
        }
        else{
            perror("read");
            // return -1;
        }
    }
    #else
    //使用sendfile效率更高---号称“零拷贝函数”，减少了在内核态的拷贝
    int size = lseek(fd, 0, SEEK_END);   //通过指针从头到尾的偏移量获得文件的大小---此时指针已经移动到文件尾部了
    lseek(fd, 0, SEEK_SET);
    off_t offset = 0;      //设置偏移量位置
    while(offset < size) {
        int ret = sendfile(cfd, fd, &offset, size-offset);    //从偏移处开始发数据，发完之后更新偏移量
        if(ret == -1) {
            perror("sendfile");
        }
    }
    #endif
    close(fd);
    return 0;
}

int sendHeadMsg(int cfd, int status, const char *descr, const char *type, int length)
{
    char buf[4096] = { 0 };
    //状态行
    sprintf(buf, "http/1.1 %d %s\r\n", status, descr);    //c语言中sprintf用于拼接字符串
    //响应头
    sprintf(buf + strlen(buf), "content-type：%s\r\n", type);
    printf("content_type: %s", type);
    sprintf(buf + strlen(buf), "content-length：%d\r\n\r\n", length);    //相应部分的空行也放这了（两个换行符：\r\n）
    //发送
    send(cfd, buf, strlen(buf), 0);
    return 0;
}
