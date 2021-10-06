#ifndef SEM_H
#define SEM_H

#include <semaphore.h>
#include <exception>

class SEM {
public:
    SEM () {//构造函数
        if (sem_init(&m_sem, 0, 0) != 0) {
            // throw std::exception();
        }
    }

    SEM (int num) {//构造函数, 初始化信号量有多少
        if (sem_init(&m_sem, 0, num) != 0) {
            // throw std::exception();
        }
    }

    ~SEM () {//析构函数
        if (sem_destroy(&m_sem) != 0) {
            // throw std::exception();
        }
    }

    bool waitSem () {//等待信号量
        return sem_wait(&m_sem) == 0;
    }

    bool post () {// 增加信号量
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;//信号量

};

#endif