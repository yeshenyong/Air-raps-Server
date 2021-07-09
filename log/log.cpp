#include "log.h"
#include <string.h>

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

/* 关闭打印文件 */
Log::~Log()
{
    if ( m_fp != NULL )
        fclose( m_fp );
}

/* 异步需要设置阻塞队列的长度，同步不需要设置 */
/*
    1.初始化异步或同步
    2.若为异步创建阻塞队列，且创建写线程
    3.若为同步、异步除了2以外下述都一样
    4.初始化写文件名、写缓冲区、写行大小、开始写文件的时间
*/
bool Log::init( const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    /* 如果设置了max_queue_size，则设置为异步 */
    if ( max_queue_size >= 1 )
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        /* flush_log_thread为回调函数，线程工作函数 */
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    } 

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', sizeof(m_buf) );
    /* 日志最大行数 */
    m_split_lines = split_lines;

    time_t t = time( NULL );
    /* 日期格式 */
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //从后往前找到第一个 '/' 的位置
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    /* 没找到，此处使用c++11nullptr优化NULL，使用默认值 */
    if ( p == NULL ){
        /* 将可变参数 “…” 按照format的格式格式化为字符串，然后再将其拷贝至str中 */
        /* 
            %02d:默认情况下，数据数据宽度不够2位是用空格填补的，但是因为2d前面有0，
            表示，数据宽度不足时用0填补 
        */
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name );
    }
    else{
        /* 将文件名传给log_name */
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name );
    }
    m_today = my_tm.tm_mday;
    /* 
        以“追加”方式打开文件。如果文件不存在，那么创建
        一个新文件；如果文件存在，那么将写入的数据追加
        到文件的末尾（文件原有的内容保留） 
    */
    m_fp = fopen(log_full_name, "a");
    if (m_fp == nullptr)
    {
        return false;
    }
    return true;
}
/* 一行一行来读取的 */
void Log::write_log(int level, const char *format, ...)
{
    /* timval ,seconds and microsecond */
    struct timeval now = {0, 0};
    gettimeofday( &now, NULL );
    time_t t = now.tv_sec;
    /* 每次写的时候都记录时间，同步才立即做 */
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }
    /* 写入一个log，对m_count++, m_split_lines最大行数 */
    m_mutex.lock();
    m_count++;
    /* 写的时候不是今天 || m_count超过了最大行数，则新建日志 */
    if( m_today != my_tm.tm_mday || m_count % m_split_lines == 0 )
    {
        char new_log[256] = {0};
        fflush(m_fp);
        /* 
            fflush()会强迫将缓冲区内的数据写回参数stream 
            指定的文件中，如果参数stream 为NULL，fflush()
            会将所有打开的文件数据更新。 
        */
       fclose(m_fp);
       char tail[16] = {0};
       snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday );

       if ( m_today != my_tm.tm_mday )
       {
           snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
           m_today = my_tm.tm_mday;
           m_count = 0;
       }
       else
       {
           snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
       }
       m_fp = fopen(new_log, "a");
    }
    m_mutex.unlock();

    va_list valst;
    /* 
        VA_LIST 是在C语言中解决变参问题的一组宏
        ，用于获取不确定个数的参数 
    */
    va_start(valst, format);

    string log_str;
    m_mutex.lock();
    /* 写入的具体时间内容格式 */
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s );
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;
    m_mutex.unlock();

    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }
    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    /* 强制刷新写入流缓冲区 */
    fflush(m_fp);
    m_mutex.unlock();
}
