#ifndef LOCKER_H
#define LOCKER_H

/*
    本程序实现对互斥锁类进行封装
    wenp
*/
#include <pthread.h>
#include <exception>

class Locker {

public:

    Locker () {//构造函数,对锁进行初始化
        if (pthread_mutex_init(&m_mutex, nullptr) != 0) {//锁初始化,第一个参数是目标锁，第二个参数设置互斥锁的属性，设置为nullptr使用默认属性
            // throw std::exception();//抛出异常
        }
    }

    ~Locker () {//析构函数，销毁锁
        if (pthread_mutex_destroy(&m_mutex) != 0) {//用于销毁互斥锁，释放其占用的内核资源。销毁一个已经加锁的互斥锁会导致不可预期的后果
            // throw std::exception();//抛出异常
        }
    }

    bool lock () {//加锁
        //以原子操作的方式给一个互斥锁加锁.如果目标互斥锁已经被上锁，在调用此函数将阻塞，
        //直到该互斥锁的占有者将其释放
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock () {//解锁操作
        //函数以原子操作的方式给一个互斥锁解锁，如果此时有其他线程正在等待这个互斥锁
        //则这些线程中某一个将获得它
        return pthread_mutex_unlock(&m_mutex) == 0;
    }    

    pthread_mutex_t* getLocker () {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;//互斥锁，成员变量

};
#endif
