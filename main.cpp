#include "./config/config.h"

int main( int argc, char *argv[] )
{
    //需要的数据库信息，登录名与密码
    string user = "debian-sys-maint";
    string passwd = "o4oweiH7IQ49e4cW";
    string dbName = "yourdb";

    //命令行解析，调参数
    Config config;
    config.parse_arg( argc, argv );

    WebServer server;

    server.init(config.PORT, user, passwd, dbName, config.LogWrite,
                config.OPT_LINGER, config.TRIGMode, config.sql_num,
                config.thread_num, config.close_log, config.act_model );

    /* 日志初始化开启 */
    server.log_write();
    
    /* 数据库连接池开启 */
    server.sql_pool();

    /* 线程池开启 */
    server.thread_pool();
    /* 触发模式，ET（edge trigger）、LT(level trigger) */
    server.trig_mode();

    /* 网络编程监听 */
    server.eventListen();

    /* 运行 */
    server.eventLoop();
    return 0;
}