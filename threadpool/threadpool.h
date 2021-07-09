#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template < class T >
class threadpool
{
    public:
        /* actor_model表示使用Proactor还是reactor thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
        threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
        ~threadpool();
        bool append(T *request, int state);
        bool append_p(T *request);

    private:
        /* 工作线程运行队列，它不断从工作队列中取出任务并执行之 */
        static void *worker(void *args);
        void run();

    private:
        pthread_t *m_threads;   //线程池数组
        int m_thread_number;    //线程池中的线程数
        int m_max_requests;     //请求队列中允许的最大请求数
        std::list<T *> m_workqueue; //请求队列
        locker m_queuelocker;   //保护请求队列的互斥锁
        sem m_queuestat;        //是否有任务需要处理
        connection_pool *m_connPool;    //数据库连接池
        int m_actor_model;      //模型切换

};

template< class T >
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_request) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_request), m_threads(NULL),m_connPool(connPool)
{
    if ( thread_number <= 0 || max_request <= 0 )
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; i++)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        /* 将线程进行分离后，不用单独对工作线程进行回收 */
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
    
}


template<class T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

template<class T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<class T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<class T>
void *threadpool<T>::worker(void *args)
{
    threadpool *pool = (threadpool *)args;
    pool->run();
    return pool;
}

template<class T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        /* Proactor */
        // -a，选择反应堆模型，默认Proactor
        // 0，Proactor模型
        // 1，Reactor模型
        //improv标志在read_once和write成功后,都会被设置为1,在相应的request处理完成后,均会被设置位
        // 为0,所以这个标志是用来判断上一个请求是否已处理完毕
        if ( 1 == m_actor_model )
        {
            /* 读为0，写为1 */
            if ( 0 == request->m_state )
            {
                /* 主线程读取数据 */
                if ( request-> read_once() )
                {
                    request->improv = 1;
                    /* 数据库连接池获取数据了连接 */
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
                
            }
            else
            {
                if ( request->write() )
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }

        }
        else
        {
            connectionRAII mysql(&request->mysql, m_connPool);
            request->process();
        }
    }
    
}



#endif // THREADPOOL_H