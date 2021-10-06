/*
    本程序是对线程进行封装，形成线程池
    创建线程，并将其设置分离，系统自动回收资源
*/
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include "locker.h"
#include "sem.h"

//线程池封装类
//定义成模板类，为了代码复用，工作内容可能有很多种，不一定是http请求解析
template <typename T>
class ThreadPool {
    public:
        ThreadPool(int thread_number = 2, int max_requests = 500);//构造函数
        ~ThreadPool();//析构函数

        bool appendtoPool(T* request);// 向线程池中增加请求
        int get_thread_number();    // 返回线程池数量

    private:
        static void* work(void* arg);
        void run();

    private:
        int m_thread_number;//线程数量
        pthread_t* m_threads;//描述线程池数组的指针，大小为m_thread_number
        int m_max_requests;//请求队列中最多允许的、等待请求处理的数量
        std::list<T*> m_workqueue;//请求队列
        Locker m_workqueuelocker;//请求队列的锁，由于所有线程共享一个请求队列，必须加锁
        SEM m_workqueueSem;//请求队列是否有任务需要处理
        bool m_stop;//是否结束线程
};

template<typename T>
int ThreadPool<T>::get_thread_number(){
    return m_thread_number;
}

template<typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests) :
    m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false),
    m_threads(nullptr) {//构造函数
    
    if (thread_number <= 0 || max_requests <= 0) {//非法输入
        throw std::exception();
    }
    m_threads = new pthread_t[thread_number];//动态创建线程数组

    //创建线程并使得他们脱离，以让系统自动回收线程资源
    for (int i = 0; i < thread_number; ++i) {
        //std::cout << "正在创建线程：" << i << std::endl;
        int res = pthread_create(m_threads + i, nullptr, work, this);//创建线程
        if (res != 0) {
            delete[] m_threads;
            m_threads = nullptr;
        }

        res = pthread_detach(m_threads[i]);//设置线程分离，让系统自动回收资源
        if (res != 0) {
            delete[] m_threads;
            m_threads = nullptr;
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool () {//析构函数
    delete[] m_threads;
    m_threads = nullptr;
    m_stop = true;//线程结束，停止运行
}

template<typename T>
bool ThreadPool<T>::appendtoPool (T* request) {//向线程池中增加请求
    //操作工作队列时候一定加锁
    m_workqueuelocker.lock();//对工作队列加锁
    if (m_workqueue.size() > m_max_requests) {//当前已经到达最大请求队列上限，无法在继续
        m_workqueuelocker.unlock();//解锁
        return false;
    }
    m_workqueue.push_back(request);//将请求加入工作队列
    m_workqueuelocker.unlock();//解锁
    m_workqueueSem.post();//工作对象信号量+1
    return true;
}

template<typename T>
void* ThreadPool<T>::work(void* arg) {//子线程所要执行的内容
    ThreadPool* pool = (ThreadPool*)arg;//arg传入的this,进行强制类型转换
    pool->run();//让子线程运行起来，则子线程创建开始就一直在运行等待
    return pool;
}

template<typename T>
void ThreadPool<T>::run () {
    while (!m_stop) {//一直运行，直到命令线程终止
        m_workqueueSem.waitSem();//一直阻塞等待有任务进入
        m_workqueuelocker.lock();//等到有任务了，开始加锁
        if (m_workqueue.empty()) {//为什么要在此检查呢？
            m_workqueuelocker.unlock();//解锁
            continue;
        }
        //如果队列不为空，说明真的存在任务来了
        T* request = m_workqueue.front();//取出任务
        m_workqueue.pop_back();//从工作队列中删除任务
        m_workqueuelocker.unlock();//解锁
        if (!request) {
            continue;
        }
        request->process();
    }
}


#endif