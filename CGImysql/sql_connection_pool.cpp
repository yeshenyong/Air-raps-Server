#include "sql_connection_pool.h"
#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
connection_pool::connection_pool()
{
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
    /* 内部静态变量，懒汉模式，C++11之后无需加双重锁，static线程安全 */
    static connection_pool connPool;
    return &connPool;
}

/* 初始化 */
void connection_pool::init(string url, string User, string Password, string dbName, int port, int MaxConn, int close_log )
{
    m_url = url;
    m_Port = port;
    m_User = User;
    m_Password = Password;
    m_dbName = dbName;
    m_close_log = close_log;

    for (int i = 0; i < MaxConn; i++)
    {
        MYSQL *con = NULL;
        con = mysql_init(con);
        if ( con == NULL )
        {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), Password.c_str(), dbName.c_str(), port, NULL, 0 );
        if ( con == NULL )
        {
            LOG_ERROR("MySQL Error already mysql_real_connect");
            exit(1);
        }
        connList.push_back( con );
        ++m_FreeConn;
    }
    reserve = sem( m_FreeConn );
    m_MaxConn = m_FreeConn;
}

/* 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数 */
MYSQL *connection_pool::GetConnection()
{
    MYSQL *con = NULL;
    if ( 0 == connList.size() )
        return NULL;
    /* 大于0才可以走下去 */
    LOG_INFO("connlist size = %d", connList.size());
    reserve.wait();
    lock.lock();

    con = connList.front();
    connList.pop_front();
    if (con == NULL)LOG_ERROR("%s","GET connection error");

    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();
    return con;
}

/* 释放当前使用的连接 */
bool connection_pool::ReleaseConnection(MYSQL *con)
{
    if ( NULL == con )
        return false;
    lock.lock();
    
    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();
    reserve.post();

    return true;
}

/* 销毁数据库连接池 */
void connection_pool::DestroyPool()
{
    lock.lock();

    if ( connList.size() > 0 )
    {
        for( auto it = connList.begin() ; it != connList.end() ; it++ )
        {
            MYSQL *con = *it;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }

    lock.unlock();
}

/* 当前空闲的连接数 */
int connection_pool::GetFreeConn()
{
    return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
    *SQL = connPool->GetConnection();
    conRAII = *SQL;
    poolRAII = connPool;
}
connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);
}
