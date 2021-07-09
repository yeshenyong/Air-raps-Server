#ifndef CONFIG_H
#define CONFIG_H

#include "../webServer/webserver.h"

using namespace std;

class Config
{
    public:
        Config();
        ~Config(){}
                
        void parse_arg( int argc, char *argv[] );

        //端口号
        int PORT;

        /* 日志的写入方式分同步写入和异步写入 */
        int LogWrite;

        /*
         * 触发组合模式, 
         * listenfd，connfd
         * 0表示 LT     LT
         * 1表示 LT     ET
         * 2表示 ET     LT
         * 3表示 ET     ET
        */
        int TRIGMode;

        //listenfd触发模式
        int LISTENTrigmode;

        //connfd触发模式
        int CONNTrigmode;

        //优雅关闭连接，关于keep-alive参数，详情查看http报文
        int OPT_LINGER;

        //数据库连接池数量
        int sql_num;

        //线程池内的线程数量
        int thread_num;

        //是否关闭日志
        int close_log;

        //并发模型的选择
        //0，Proactor模型,Proactor，工作线程只负责处理逻辑，I/O操作由主线程和内核完成
        //1，Reactor模型,主线程主负责文件描述符的监听，将可读可写事件交给工作线程完成
        int act_model;
};

#endif
