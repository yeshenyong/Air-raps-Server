#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../sql_connection_pool/sql_connection_pool.h"

template <typename T>
class threadpool
{
    public:
        /* thread_number是线程池中线程的数量，max_request是请求队列中最多允许的、等待处理的请求 */
        threadpool(connection_pool *connPool, int thread_number = 8, int max_request = 10000);
        ~threadpool();
        bool append(T *request);
    private:
        /* 工作线程运行的函数，它不断从工作队列中取出任务并执行之 */
        static void *worker( void *arg );
        void run();
    private:
        int m_thread_number; //线程池中的线程数
        int m_max_requests; //请求队列中允许的最大请求数
        pthread_t *m_threads;   //描述线程池的数组，其大小为m_thread_number
        std::list<T *> m_workqueue;    //请求队列
        locker m_queuelocker;       //保护队列的互斥锁
        sem m_queuestat;       //是否有任务需要处理pv操作
        bool m_stop;        //是否结束线程
        connection_pool *m_connPool;      //数据库池
};

template<typename T>
threadpool<T>::threadpool(connection_pool *connPool, int thread_number, int max_request)
    :m_thread_number(thread_number), m_max_requests(max_request), m_stop(false),m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_request <= 0)
        throw std::exception();
    //创建储存多个线程的数组
    m_threads = new pthread_t[ m_thread_number ];
    if (!m_threads)
        throw std::exception();
    for(int i = 0; i < thread_number; ++i )
    {   
        //创建线程，储存在数组当中,标识符
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete []m_threads;
            throw std::exception();
        }
        //pthread_detach()即主线程与子线程分离，
        //子线程结束后，资源自动回收
        //pthread_detach() 在调用成功完成之后返回零
        if(pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool()
{
    //销毁线程池
    delete[] m_threads;
    m_stop = true;
}
template<typename T>
bool threadpool<T>::append(T *request)
{
    //操作工作队列加锁，加工作来了，家工作
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    //工作又来了，pv加1，工作量+1
    m_queuestat.post();
    return true;
}

template<typename T>
void *threadpool<T>::worker( void *arg )
{
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}
template<typename T>
void threadpool<T>::run()
{
    //等待有工作量我才可以做啊
    //等待大于0的时候进入，并减一
    //小于0的时候阻塞
    while( !m_stop )
    {
        m_queuestat.wait();
        m_queuelocker.lock();

        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;   
        //去获得一个数据连接来处理客户请求
        connectionRAII mysqlcon(&request->mysql, m_connPool);
        request->process();
    }
}

#endif // THREADPOOL_H