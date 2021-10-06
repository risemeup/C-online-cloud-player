/*
    web服务器的主程序，采用模拟proactor模式
    主线程主要进行监听，来进行数据读写；
    将准备好的数据发送给工作线程来处理
*/
#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>
#include <exception>
#include <error.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include "http_conn.h"
#include "threadpool.h"
#include "_freecplus.h"

using namespace std;

#define MAX_EVENT_NUMBER 500  // 监听的最大的事件数量
#define MAX_FD 1000 // 最大文件描述符个数


extern int setNoBlock (int fd);
extern void adfd (int epollfd, int fd, bool oneshoot);
extern void removefd (int epollfd, int fd);
extern void modfd (int epollfd, int fd, int ev);
extern void deal_timer ();
static int pipefd[2];


//信号捕捉，当客户端断开以后，防止服务器还持续的向客户端发送数据
void addsig () {
    struct sigaction act;
    memset(&act, '\0', sizeof(act));//将数据清空
    act.sa_handler = SIG_IGN;//捕捉到信号以后忽略
    sigemptyset(&act.sa_mask);//清空临时阻塞信号集合
    sigaction(SIGPIPE, &act, nullptr);//捕捉信号
}

void sig_handler (int sig) {
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void addSig (int sig) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;  // 如果系统调用被中断，则信号处理完成之后会继续系统调用
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

CLogFile logfile;

/*主函数*/
int main (int argc, char* argv[]) {

    if (argc <= 2) {
        cout << "Format：./server 端口号 日志路径\nSample: ./server 5005 /tmp/server.log\n" << endl;
        return 1;
    }

    if(logfile.Open(argv[2]) == false){
        printf("log open %s failed.\n", argv[2]);
        return -1;
    }

    logfile.Write("\tServer start.\n");

    int userport = atoi(argv[1]);//将字符串端口转换为整数端口

    //进行信号捕捉
    addsig();

    ThreadPool<http_conn> * threadpool = nullptr;//创建线程池指针
    try {
        threadpool = new ThreadPool<http_conn>(2, 500);
        logfile.Write("\tCreate %d threads success\n", threadpool->get_thread_number());
    }
    catch (...) {
        logfile.Write("\tCreate threadpool failed\n");
        return 1;
    }

    http_conn* users = new http_conn[MAX_FD];//最大客户端数量，最大监听文件描述符

    //socket通信，TCP协议
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);//创建监听套接字
    if (listenfd == -1) {
        logfile.Write("\tCreate listen socket failed\n");
        exit(-1);
    }
    //设置端口复用
    // int opt = 1;
    // int res = setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    // if (res == -1) {
    //     logfile.Write("\tSet port multiplexing failed\n");
    //     exit(-1);
    // }

    int res = 0;
    sockaddr_in saddr;//服务器地址和端口
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(userport);//定死服务器端口
    saddr.sin_addr.s_addr = INADDR_ANY;//本机任何一个ip
    res = bind(listenfd, (const sockaddr*)&saddr, sizeof(saddr));//绑定端口
    if (res == -1) {
        logfile.Write("\tBind port failed\n");
        return -1;
    }

    res = listen(listenfd, 5);//开始监听
    if (res == -1) {
        logfile.Write("\tListen failed\n");
        exit(-1);
    }

    //使用epoll 实现端口复用
    int epollfd = epoll_create(5);//在内核中创建一块文件描述符内存，参数没有任何含义，大于0即可
    if (epollfd == -1) {
        logfile.Write("\tCreate epoll failed\n");
        exit(-1);
    }

    struct epoll_event events[MAX_EVENT_NUMBER];//创建最大可监听事件数量的数组
    adfd(epollfd, listenfd, false);//将监听文件描述符添加到epollfd中，内核中的文件描述符区，用于监听

    http_conn::m_epollfd = epollfd;//所有线程共享一个内核事件描述文件描述符
    
    //创建管道用于捕捉定时器到时事件
    res = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(res != -1);
    setNoBlock(pipefd[1]);
    adfd(epollfd, pipefd[0], false);//将读事件加入epollfd

    //设置信号处理函数
    addSig(SIGALRM);

    bool timeout = false;//设置定时器到时标志
    bool have_del = false;
    alarm(5);//定时5秒后产生SIGALARM信号

    while (true) {//主线程模拟proactor模式，一直监听客户端的介入，并将数据读写完毕后，交给工作线程处理
        int recnum = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);//阻塞等待
        if (( recnum < 0 ) && ( errno != EINTR ) ) {//失败,或者因为中断而造成的错误
            logfile.Write("\there wrong!\n");
            continue;
        }
        
        //循环读取出epollfd中返回的监听到变化的文件描述符，并将其内容读取出来
        for (int i = 0; i < recnum; ++i) {
            int curfd = events[i].data.fd;//获取当前文件描述符

            /***********说明有客户端接入***************/
            if (curfd == listenfd) {
                // printf("Current number of connection: %d\n", http_conn::m_user_count);
                sockaddr_in caddr;
                socklen_t len  = sizeof(caddr);
                int clientfd = accept(curfd, (sockaddr*)&caddr, &len);//接收客户端
                if (clientfd == -1) {
                    logfile.Write("\tAccept new connection failed\n");
                    continue;
                }
                //判断当前用户数量
                if (http_conn::m_user_count >= MAX_FD) {//不能在接入新的连接了
                    close(curfd);
                    continue;
                }

                logfile.Write("\tNew connection: current client number:%d.  client: %s/%d\n", http_conn::m_user_count, inet_ntoa(caddr.sin_addr), caddr.sin_port);
                //当前用户数量还没有达到上限，还可以继续加入
                users[clientfd].initNewConn(clientfd, caddr);
            }
            else if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {//客户端已关闭
                users[curfd].closeConn();//关闭当前通信套接字的连接       
            }
            else if ((curfd == pipefd[0]) && (events[i].events & EPOLLIN)) {/****已经到5秒时间，每到5时间就会发送一个信号*****/
                 //说明有定时器到时了
                char signals[1024];
                res = recv(pipefd[0], signals, sizeof(signals), 0);
                if (res == -1) {
                    continue;
                }
                else if (res == 0) {//客户端断开连接
                    continue;
                }
                else {
                    for (int i = 0; i < res; ++i) {
                        if (signals[i] == SIGALRM) {
                            //用timeout变量标记有定时器任务需要处理，但不立即处理定时器任务
                            //这是因为定时任务的优先级不高，我们优先处理其他更重要的任务
                            timeout = true;
                            break;
                        }
                    }
                }
            }
            else if (events[i].events & EPOLLIN) {//检测到读事件
                if (users[curfd].readRequest()) {//一次性读取所有数据，然后将数据传递给子线程
                    logfile.Write("\tRead accessed!\n");
                    threadpool->appendtoPool(users + curfd);
                }
                else {//如果读取数据失败了，则要关闭这个连接
                    logfile.Write("\tRead failed!\n");
                    users[curfd].closeConn();           
                }
            }
            else if (events[i].events & EPOLLOUT) {//检测到写事件
                if (!users[curfd].writetoClient()) {   
                    users[curfd].closeConn();
                }
            }
            //最后处理定时事件，因为I/O事件有更高优先级。当然，这样做将导致定时任务不能按照精准的预定时间执行
            if (timeout) {//有到期任务
                deal_timer();
                //因为一次alarm调用只会引起一次SIGALARM信号，所以我们要重新定时，以不断触发SIGALARM信号
                alarm(5);
                timeout = false;
            }
        }
    }
    logfile.Write("\tEnd1!\n");
    close(epollfd);
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    logfile.Write("\tEnd2!\n");
    delete[] users;
    delete threadpool;
    return 0;
}