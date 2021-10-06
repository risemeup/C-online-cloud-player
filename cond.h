#ifndef COND_H
#define COND_H

#include <pthread.h>
#include <exception>

class COND {

public:
    COND () {//构造函数
        if (pthread_cond_init(&m_cond, nullptr) != 0) {//初始化条件变量，nullptr为设置默认属性
            // throw std::exception();
        }
    }

    ~COND () {//析构函数
        if (pthread_cond_destroy(&m_cond) != 0) {//销毁条件变量
            // throw std::exception();
        }
    }

    bool wait (pthread_mutex_t* m_mutex) {
        // 一直阻塞等待
        return pthread_cond_wait(&m_cond, m_mutex) == 0;
    }

    bool timedwait (pthread_mutex_t* m_mutex, const struct timespec* t) {
        //等待一定时间
        return pthread_cond_timedwait(&m_cond, m_mutex, t) == 0;
    }

    bool signal () {//用于唤醒一个等待目标条件变量的线程，至于哪个线程被唤醒，这个取决于线程的优先级
        return pthread_cond_signal(&m_cond) == 0;
    }

    bool broadcast () {//以广播的方式唤醒所有等待目标条件变量的线程
        return  pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;//条件变量

};

#endif

