#ifndef SORT_TIMER_LIST_H
#define SORT_TIMER_LIST_H

#include "utill_timer.h"
#include "http_conn.h"

//定时器链表，它是一个升序、双向链表，并且带有头节点和尾节点
class sort_timer_list {
public:
    //构造函数
    sort_timer_list():m_head(nullptr), m_tail(nullptr) {}
    //析构函数
    ~sort_timer_list() {
        utill_timer* temp = m_head;
        while (temp) {//循环删除，从头删到尾部
            m_head = temp->m_next;
            delete temp;
            temp = m_head;
        }
    }

    //判断链表是否为空
    bool isEmpty () {
        return m_head == nullptr && m_tail == nullptr;
    }

    /*逻辑功能函数*/
    //将目标定时器timer添加到链表中
    void add_timer(utill_timer* timer);
    /*
        当某个定时器任务发生变化时，调整对应的定时器在链表中的位置。
        这个函数只考虑被调整的定时器的超时时间延长的情况，即该定时器需要向链表尾部移动
    */
    void adjust_timer(utill_timer* timer);
    //将目标定时器timer从链表中删除
    void del_timer(utill_timer* timer);
    /*
        SIGALARM信号每次触发就在其信号处理函数中执行一次tick()函数，
        以处理链表上到期任务
    */
    // //任务的回调函数
    void (*cb_func)();
    void tick();
private:
    /*
        一个重载的辅助函数，它被公有的add_timer函数和addjust_timer函数调用
        该函数表示将目标定时器timer添加到节点list_head之后的部分链表中
    */
   void add_timer(utill_timer* head, utill_timer* timer);

private:
    utill_timer* m_head;//头节点
    utill_timer* m_tail;//尾节点

};

#endif