#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <iostream>
#include "../CGImysql/sql_connection_pool.h"
#include "../http/http_con.h"
#include "../threadpool/threadpool.h"
#include "../timer/lst_timer.h"
/*
 * WebServer管理类
 * 1.负责数据库连接
 * 2.建立监听、bind等网络接口
 * 3.类似工厂类
*/

const int MAX_FD = 65536;   //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;     //最小超时单位

class WebServer
{
    public:
        WebServer();
        ~WebServer();

        /* 初始化服务器 */
        void init( int port, std::string user, std::string passWord, std::string dbName,
                   int log_write, int opt_linger, int trigmode, int sql_num,
                   int thread_num, int close_log, int actor_model );

        /* 初始化日志写入 */
        void log_write();

        /* 数据库连接池 */
        void sql_pool();

        /* 线程池 */
        void thread_pool();

        /* 触发模式 */
        void trig_mode();

        /* 网络编程基本步骤 */
        void eventListen();

        /* 运行 */
        void eventLoop();


        /* 新用户连接sockfd处理 */
        bool dealclientdata();

        /* 有一端连接关闭，服务器关闭对应的timer */
        void deal_timer(util_timer *timer, int sockfd);

        /* 新连接，加定时器 */
        void timer(int connfd, struct sockaddr_in client_address);

        /* 信号处理 */
        bool dealwithsignal(bool &timeout, bool &stop_server);
    
        /* 客户端来数据，处理数据 */
        void dealwithread( int sockfd );

        /* 有输出事件 */
        void dealwithwrite( int sockfd );

        /* 连接有新数据到达，重置定时器 */
        void adjust_timer( util_timer *timer );
    
    public:
        int m_port;
        
        int m_log_write;
        int m_opt_linger;
        int m_trigmode;
        int m_sql_num;
        int m_thread_num;
        int m_close_log;
        int m_actor_model;

        int m_LISTENTrigmode;
        int m_CONNTrigmode;

        int m_listenfd;
        int m_epollfd;
        int m_pipefd[2];

        /* 文件目录 */
        char* m_root;

        http_conn *users;

        //数据库相关
        std::string m_user;
        std::string m_password;
        std::string m_dbName;
        connection_pool *m_connPool;

        /* 线程池相关 */
        threadpool<http_conn> *m_pool;

        /* 定时器相关 */
        client_data *client_timer;
        Utils utils;

        //epoll_event相关
        epoll_event events[MAX_EVENT_NUMBER];

};


#endif // WEBSERVER_H