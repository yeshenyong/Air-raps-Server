#ifndef SQL_CONNECTION_POLL_H
#define SQL_CONNECTION_POLL_H

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"

using namespace std;

//数据库连接池，单例模式
class connection_pool
{
    public:
        MYSQL *GetConnection();     //获取数据库连接
        bool ReleaseConnection( MYSQL* conn );  //释放连接
        int GetFreeConn();          //获取空闲连接
        void DestroyPool();         //销毁所有连接

        //单例模式
        static connection_pool *GetInstance();

        void init( string url, string User, string PassWord,string DataBaseName, int Port, unsigned int MaxConn);

        connection_pool();
        ~connection_pool();

    private:
        unsigned int MaxConn;   //最大连接数
        unsigned int CurConn;   //当前已使用的连接数
        unsigned int FreeConn;  //当前空闲的连接数

    private:
        locker lock;
        list<MYSQL *> connlist; //连接池
        sem reserver;   //信号量PV
    private:
        string url;             //主机地址
        string Port;            //数据库端口号
        string User;            //登录数据库用户名
        string PassWord;        //登录数据库密码
        string DatabaseName;    //使用数据库名
};

// 将数据库连接的获取与释放通过RAII机制封装，避免手动释放。

class connectionRAII{

//这里需要注意的是，在获取连接时，通过有参构造对传入的参数进行修改。
// 其中数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改。
    public:
        connectionRAII( MYSQL **con, connection_pool *connPoll);
        ~connectionRAII();
    private:
        MYSQL *conRAII;
        connection_pool *poolRAII;
};


#endif // SQL_CONNECTION_POLL_H