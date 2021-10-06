#ifndef UTILL_TIMER_H
#define UTILL_TIMER_H
#include <time.h>

//定时器类
class utill_timer {
public:
    utill_timer():m_prev(nullptr), m_next(nullptr){}
public:
    //成员变量
    time_t m_expire;//任务超时时间，这里使用绝对时间
    void (*cb_func)(int);//任务回调函数，回调函数处理客户端数据，由定时器执行者传递给回调函数
    int m_user_sockfd;//用户数据结构，包括客户端socket\文件描述符等
    utill_timer* m_prev;//指向前一个定时器
    utill_timer* m_next;//指向后一个定时器
};
#endif