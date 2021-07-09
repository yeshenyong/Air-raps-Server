#include "webserver.h"

WebServer::WebServer()
{
    /* http_conn类对象 */
    users = new http_conn[MAX_FD];

    /* root文件夹路径 */
    char server_path[200];
    // getcwd()会将当前工作目录的绝对路径复制到参数buffer
    // 所指的内存空间中,参数maxlen为buffer的空间大小
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);
    /* 定时器 */
    client_timer = new client_data[MAX_FD];
}
WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    delete[] users;
    delete[] client_timer;
    delete m_pool;
}



void WebServer::log_write()
{
    if ( 0 == m_close_log )
    {
        //初始化日志，0为同步，1为异步
        if ( 1== m_log_write )
            Log::get_instance()->init("./ysyServer", m_close_log, 2000, 800000, 800 );
        else
            Log::get_instance()->init("./ysyServer", m_close_log, 2000, 800000, 0 );
    }
}

void WebServer::sql_pool()
{
    /* 初始化数据库连接池 */
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_password, m_dbName, 3306, m_sql_num, m_close_log );
    /* 初始化数据库读取表 */
    users->initmysql_result(m_connPool);
}


/* 服务器初始化 */
void WebServer::init( int port, std::string user, std::string passWord, std::string dbName,int log_write, int opt_linger, 
                      int trigmode, int sql_num,int thread_num, int close_log, int actor_model )
{
    m_port = port;
    m_user = user;
    m_password = passWord;
    m_dbName = dbName;
    m_log_write = log_write;
    m_opt_linger = opt_linger;
    m_sql_num = sql_num;
    m_trigmode = trigmode;
    m_thread_num = thread_num;
    m_close_log = close_log;
    m_actor_model = actor_model;
}

/* 线程池初始化 */
void WebServer::thread_pool()
{   
    /* 线程池 */
    m_pool = new threadpool<http_conn>( m_actor_model, m_connPool, m_thread_num );
}

/* 触发模式初始化 */
void WebServer::trig_mode()
{
    /* LT  + LT */
    if ( 0 == m_trigmode )
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    /* LT + ET */
    else if ( 1 == m_trigmode )
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    /* ET + LT */
    else if ( 2 == m_trigmode )
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    /* ET + ET */
    else if ( 3 == m_trigmode )
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::eventListen()
{
    /* 网路编程基本步骤 */
    m_listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert(m_listenfd >= 0);

    /* 优雅关闭连接 */
    /*
        处理方式无非两种：
        丢弃或者将数据继续发送至对端，
        优雅关闭连接
    */
   if ( 0 == m_opt_linger )
   {
       struct linger tmp = {0, 1};
       setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
   }
   else if ( 1 == m_opt_linger )
   {
       struct linger tmp = {1, 1};
       setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
   }

   int ret = 0;
   struct sockaddr_in address;
   bzero( &address, sizeof(address) );
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_ANY);
   address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address) );
    assert( ret >= 0 );
    ret = listen( m_listenfd, 5 );
    assert( ret >= 0 );   

    utils.init( TIMESLOT );

    /* epoll创建内核事件表 */
    // epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert( m_epollfd != -1 );

    utils.addfd( m_epollfd, m_listenfd, false, m_LISTENTrigmode );
    http_conn::m_epollfd = m_epollfd;
    
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert( ret != -1 );

    /* 写端设置为非阻塞 */
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    /* 工具类、信号和描述符基础操作 */
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while ( !stop_server )
    {
        int number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if ( number < 0 && errno != EINTR )
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            /* 处理到新连接 */
            if ( sockfd == m_listenfd )
            {
                bool flag = dealclientdata();
                if ( !flag )
                    continue;
            }
            else if ( events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                /* 服务器关闭连接，移除对应的计时器 */
                util_timer *timer = client_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            /* 处理信号 */
            else if ( ( sockfd == m_pipefd[0] ) && (events[i].events & EPOLLIN) )
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (!flag)
                    LOG_ERROR("%s", "dealwithsignal failure");
            }
            /* 处理客户连接上接收到的数据 */
            else if ( events[i].events & EPOLLIN )
            {
                dealwithread(sockfd);
            } 
            else if ( events[i].events & EPOLLOUT )
            {
                dealwithwrite(sockfd);
            }
        }
        if ( timeout )
        {
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}

void WebServer::dealwithread( int sockfd )
{
    util_timer *timer = client_timer[sockfd].timer;
    /* Reactor，读取事件交给工作线程处理 */
    if ( 1 == m_actor_model )
    {
        if ( timer )adjust_timer( timer );
        /* 若检测到读事件，将该事件放入请求队列 */
        m_pool->append( users + sockfd, 0 );

        while( true )
        {
            if ( 1 == users[sockfd].improv )
            {
                if ( 1 == users[sockfd].timer_flag )
                {
                    deal_timer( timer, sockfd );
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    /* proactor，读取事件交给主线程处理 */
    else
    {
        if ( users[sockfd].read_once() )
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            /* 若检测到读事件 */
            m_pool->append_p( users + sockfd );
            if ( timer )
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer( timer, sockfd );
        }
    }
}

void WebServer::dealwithwrite( int sockfd )
{
    util_timer *timer = client_timer[sockfd].timer;
    /* reactor，读写放在多线程 */
    if ( 1 == m_actor_model )
    {
        if ( timer )
        {
            adjust_timer( timer );
        }
        m_pool->append( users + sockfd, 1 );
        while( true )
        {
            if ( 1 == users[sockfd].improv )
            {
                if ( 1 == users[sockfd].timer_flag )
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if ( users[sockfd].write() )
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            if ( timer )adjust_timer(timer);    
        }
        else{
            deal_timer(timer, sockfd);
        }
    }
}


void WebServer::adjust_timer( util_timer *timer )
{
    time_t cur = time( NULL );
    timer->time = cur + 3 * TIMESLOT;
    utils.m_timer_list.adjust_timer( timer );

    LOG_INFO("%s", "adjust timer once");
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv( m_pipefd[0], signals, sizeof(signals), 0 );
    if ( ret == -1 )
        return false;
    else if ( ret == 0 )
        return false;
    else
    {
        for (int i = 0; i < ret; i++)
        {
            switch (signals[i])
            {
            case SIGALRM:
                timeout = true;
                break;
            case SIGTERM:
                stop_server = true;
                break;
            default:
                break;
            }
        }
    }
    return true;
}

bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    /* LT,不需要一次读完，后面会继续发信号 */
    if ( 0 == m_LISTENTrigmode )
    {
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
        if ( connfd < 0 )
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        } 
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        /* 加定时器 */
        timer(connfd ,client_address);
    }
    /* ET，需要一次读完，后面不会继续发信号 */
    else
    {
        while ( 1 )
        {
            int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
            if ( connfd < 0 )
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }         
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }  
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

/* 加定时器 */
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    /* http_conn中初始化 */
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_password, m_dbName);

    /* 创建client_data数据 */
    /* 创建定时器，设置回调函数和超时事件，绑定用户数据，将定时器添加到链表中 */
    client_timer[connfd].address = client_address;
    client_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &client_timer[connfd];
    timer->cb_func = cb_func;

    time_t cur = time( NULL );
    timer->time = cur + 3 * TIMESLOT;
    client_timer[connfd].timer = timer;

    utils.m_timer_list.add_timer(timer);

}

void WebServer::deal_timer( util_timer *timer , int sockfd )
{
    timer->cb_func( &client_timer[sockfd] );
    if (timer)
    {
        utils.m_timer_list.del_timer(timer);
    }
    LOG_INFO("close fd %d", client_timer[sockfd].sockfd);
}
