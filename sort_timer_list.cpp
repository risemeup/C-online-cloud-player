#include <iostream>
#include "sort_timer_list.h"
#include "_freecplus.h"

extern CLogFile logfile;

//将目标定时器timer添加到链表中
void sort_timer_list::add_timer (utill_timer* timer) {
    if (timer == nullptr) return;//传入的参数为空，无需插入

    if (m_head == nullptr) {//如果头结点也为空的话，那么就让这个参数当头结点
        m_head = timer;
        m_tail = m_head;//此时头结点尾节点是同一个节点
        return;
    }

    /*
        如果目标定时器的超时时间小于当前链表中所有定时器的超时时间
        那么就把该定时器作为链表的头节点插入，作为新的链表头
        否则就调用重载的add_timer()函数，把它插入在链表的合适位置
    */
    if (timer->m_expire < m_head->m_expire) {//链表是按照递增顺序组织的，所以比头结点小一定比后面节点小
        timer->m_next = m_head;
        m_head->m_prev = timer;
        m_head = timer;
        return;
    }
    //否则此时就要在链表中寻找一个合适的位置插入当前定时器
    add_timer(m_head, timer);
}



/*
    当某个定时器任务发生变化时，调整对应的定时器在链表中的位置。
    这个函数只考虑被调整的定时器的超时时间延长的情况，即该定时器需要向链表尾部移动
*/
void sort_timer_list::adjust_timer (utill_timer* timer) {
    if (timer == nullptr) return;//异常输入
   
    //如果被调整的定时器是位于链表末尾，或者该定时器新的超时时间值仍然小于其下一个定时器的时间，则不用调整
    if (timer->m_next == nullptr || (timer->m_expire <= timer->m_next->m_expire)) {
    }
    else if (timer == m_head) {//如果目标定时器是链表的头节点，则将该定时器从链表中取出并重新插入链表
        m_head = m_head->m_next;
        m_head->m_prev = nullptr;
        timer->m_next = nullptr;
        add_timer(m_head, timer);//重新插入队列

    }
    else {//如果目标定时不是头节点则将其取出，重新插入定时器链表
        timer->m_prev->m_next = timer->m_next;
        timer->m_next->m_prev = timer->m_prev;
        timer->m_prev = nullptr;
        timer->m_next = nullptr;
        add_timer(m_head, timer);        
    }   
}


//将目标定时器timer从链表中删除
void sort_timer_list::del_timer (utill_timer* timer) {
    if (timer == nullptr) return;//异常输入

    //下面这个条件成立表示链表中只有一个定时器，即目标定时器
    if ((timer == m_head) && (timer == m_tail)) {
        delete timer;
        m_head = nullptr;
        m_tail = nullptr;
    }
    else if (timer == m_head) {//如果目标节点为头节点，并且链表中有多个节点
        //直接将该头节点删除即可
        m_head = m_head->m_next;
        m_head->m_prev = nullptr;
        delete timer;
    }
    else if (timer == m_tail) {//如果目标节点为尾部节点，并且链表中有多个节点
        //直接将该尾部节点删除即可
        m_tail = m_tail->m_prev;
        m_tail->m_next = nullptr;
        delete timer;
    }
    else {//此时节点位于中间位置，并且链表有多个节点，将其取出后删除
        timer->m_prev->m_next = timer->m_next;
        timer->m_next->m_prev = timer->m_prev;
        delete timer;
    }
}

/*
    SIGALARM信号每次触发就在其信号处理函数中执行一次tick()函数，
    以处理链表上到期任务
*/

void sort_timer_list::tick () {
    if (m_head == nullptr) return;

    // std::cout << "timer tick" << std::endl;

    time_t curTime = time(nullptr);//获取当前系统时间
    utill_timer* temp = m_head;
    //从头节点开始依次处理每个定时器，直到遇到一个尚未到期的定时器
    while (temp != nullptr) {
        /*
            因为每个定时器都使用绝对时间作为超时值，所有可以把定时器的超时值和
            系统当前时间进行比较，判断是否到期
        */
       if (curTime < temp->m_expire) {//还没到期
            break;//当前节点小于则直接跳出，因为后面的节点也全部都小于，并且前面不小于的已经被处理了
       }

       //调用定时器的回调函数，执行定时任务，主要是进行资源释放，连接关闭
       temp->cb_func(temp->m_user_sockfd);
       //执行完定时器中的定时任务之后，就将它从链表中删除，并且重置链表头节点
       m_head = temp->m_next;
       if (m_head != nullptr) {
           m_head->m_prev = nullptr;
       }
       //std::cout << "删除一个到时任务" << std::endl;
       delete temp;
       temp = m_head;
       
       logfile.Write("\tDelete some connections.  current clinent number: %d\n", http_conn::m_user_count);
    }
}

/*
    一个重载的辅助函数，它被公有的add_timer函数和addjust_timer函数调用
    该函数表示将目标定时器timer添加到节点list_head之后的部分链表中
*/
void sort_timer_list::add_timer (utill_timer* head, utill_timer* timer) {
    utill_timer* pre = head;
    utill_timer* cur = head->m_next;
    while (cur != nullptr) {//不为空
        if (timer->m_expire < cur->m_expire) {//找到了合适的插入位置
            timer->m_next = cur;
            pre->m_prev = timer;
            timer->m_prev = pre;
            pre->m_next = timer;
            break;
        }
        pre = cur;
        cur = cur->m_next;
    }
    //遍历完整个计时器链表仍为找到，则把当前目标计时器作为尾节点插入
    if (cur == nullptr) {
        pre->m_next = timer;
        timer->m_prev = pre;
        timer->m_next = nullptr;
        m_tail = timer;//更新尾部节点
    }
}