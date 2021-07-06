#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"


using namespace std;

connection_pool::connection_pool()
{
    this->CurConn = 0;
    this->FreeConn = 0;
}
connection_pool *connection_pool::GetInstance()
{
    static connection_pool connPool;
    return &connPool;
}


//构造初始化
void connection_pool::init( string url, string User, string PassWord,string DataBaseName, int Port, unsigned int MaxConn)
{
    this->url = url;
    this->Port = Port;
    this->User = User;
    this->PassWord = PassWord;
    this->DatabaseName = DatabaseName;

    lock.lock();
    //创建连接池
    for (int i = 0; i < MaxConn; ++i)
    {
        MYSQL *con = NULL;
        con = mysql_init(con);

        if ( con == NULL )
        {
            cout << "Error:" << mysql_error( con );
            exit(1);
        }
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DataBaseName.c_str(), Port, NULL, 0);

        if ( con == NULL )
        {
            cout << "Error:" << mysql_error( con );
            exit(1);
        }
        connlist.push_back( con );
        ++FreeConn;
    }
    //分配pv信号量
    reserver = sem( FreeConn );

    this->MaxConn = FreeConn;

    lock.unlock();
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
    MYSQL *con = NULL;

    if (connlist.size() == 0)
        return NULL;

    //等待信号量大于0,并减一操作
    reserver.wait();
    lock.lock();
    //取出连接
    con = connlist.front();
    connlist.pop_front();

    --FreeConn;
    ++CurConn;
    lock.unlock();
    return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection( MYSQL *con )
{
    if (NULL == con)
        return false;
    lock.lock();

    connlist.push_back(con);
    ++FreeConn;
    --CurConn;
    lock.unlock();
    //pv信号量+1
    reserver.post();
    return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{
    lock.lock();
    if( connlist.size() > 0 )
    {
        //此处于源代码优化,加入C++11元素
        for( auto it = connlist.begin(); it != connlist.end(); ++it)
        {
            MYSQL *con = *it;
            mysql_close( con );
        }
        CurConn = 0;
        FreeConn = 0;
        connlist.clear();
        lock.unlock();
    }
    lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
    return this->FreeConn;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
    *SQL = connPool->GetConnection();

    conRAII = *SQL;
    poolRAII = connPool;
}
connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);
}