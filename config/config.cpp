#include "config.h"

Config::Config(){
    //端口号默认
    PORT = 9006;

    //日志写入方式，0：同步写入     1：异步写入
    LogWrite = 0;

    //触发组合模式,默认LT+LT
    TRIGMode = 0;

    //listenfd默认LT
    LISTENTrigmode = 0;

    //connfd默认LT
    CONNTrigmode = 0;

    //数据库连接池数量,默认8
    sql_num = 8;

    //线程池内的线程数量，默认8
    thread_num = 8;

    //是否关闭日志，默认否
    close_log = 0;

    //并发模型，默认Proactor
    act_model = 0;
}
void Config::parse_arg( int argc, char *argv[] ){
    int opt;
    /* ./server -p 9007 -l 1 -m 0 -o 1 -s 10 -t 10 -c 1 -a 1 */
    //返回全域变量optarg
    const char *str = "p:l:m:o:s:t:c:a:";
    //获取命令行参数
    while( ( opt = getopt(argc, argv, str) ) != -1 )
    {
        switch ( opt )
        {
            case 'p':
                PORT = atoi( optarg );
                break;
            case 'l':
                LogWrite = atoi( optarg );
                break;
            case 'm':
                TRIGMode = atoi( optarg );
                break;
            case 'o':
                OPT_LINGER = atoi( optarg );
                break;
            case 's':
                sql_num = atoi( optarg );
                break;
            case 't':
                thread_num = atoi( optarg );
                break;
            case 'c':
                close_log = atoi( optarg );
                break;
            case 'a':
                act_model = atoi( optarg );
                break;
            default:
                break;
        }
    }
}

