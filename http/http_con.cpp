#include "http_con.h"

#include <mysql/mysql.h>
#include <fstream>

/* 定义http响应的一些状态信息 */
/*
    - 4xx：客户端错误--请求有语法错误，服务器无法处理请求。
    - 400 Bad Request：请求报文存在语法错误。
    - 403 Forbidden：请求被服务器拒绝。
    - 404 Not Found：请求不存在，服务器上找不到请求的资源。

    - 5xx：服务器端错误--服务器处理请求出错。
    - 500 Internal Server Error：服务器在执行请求时出现错误。

*/
const char *ok_200_title = "OK";
const char *error_400_tile = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The request file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

void http_conn::initmysql_result(connection_pool *connPool)
{
    /* 先从连接池中取出一个连接 */
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    /* 在user表中检索username，passwd数据，浏览器端输入 */
    if ( mysql_query(mysql, "SELECT username,passwd FROM user") )
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }
    /* 从表中检索完整的结果集 */
    MYSQL_RES *result = mysql_store_result(mysql);

    /* 返回结果集中的列数 */
    int num_fields = mysql_num_fields(result);

    /* 返回所有字段结构的数组 */
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    /* 从结果集中获取下一行，将对应的用户名和密码，存入map中 */
    while( MYSQL_ROW row = mysql_fetch_row(result) )
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }

}

/* 将文件描述符设置为非阻塞 */
int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

/* 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT */
void addfd( int epollfd, int fd, bool one_shot, int TRIGMode )
{
    epoll_event event;
    event.data.fd = fd;
    /* EPOLLRDHUO 文件符被挂断 */
    if ( 1 == TRIGMode )
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot)
        event.events |= EPOLLONESHOT;
    
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

/* 从内核事件从删除描述符 */
void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );
}

/* 将事件重置为EPOLLONESHOT */
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}


int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/* 关闭连接，关闭一个连接，客户总量减一 */
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1) )
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

/*
    循环读取客户数据，直到无数据可读或对方关闭数据
    非阻塞ET工作模式下，需要一次性把数据读完
*/

bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;
    /* LT读取数据 */
    if ( 0 == m_TRIGMode )
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;
        if ( bytes_read <= 0 )
            return false;
        return true;
    }
    else
    {
        while (true)
        {
            /* 从套接字接收数据，存储在m_read_buf存储区 */
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );
            if ( bytes_read == -1 )
            {
                //非阻塞ET模式下，需要一次性把数据读完
                if ( errno == EAGAIN || errno == EWOULDBLOCK )
                    break;
                return false;
            }   
            else if ( bytes_read == 0 )
                return false;
            /* 修改m_read_idx的读取字节数 */
            m_read_idx += bytes_read;
        }
        return true;
    }
    return true;
}

/* 初始化新接受的连接 */
/* check_state默认为分析请求行状态 */
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;
    LOG_INFO("%s", "I am init");

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/* 初始化连接，外部调用初始化套接字地址 */
void http_conn::init( int sockfd, const sockaddr_in &addr, char *root, int TRIGMode, int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    /* 当浏览器出现连接重置时，可能是网站根目录出错或HTTP响应个事出错或者访问文件完全为空 */
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy( sql_user, user.c_str() );
    strcpy( sql_passwd, passwd.c_str() );
    strcpy( sql_name, sqlname.c_str() );
    
    init();
}


/*
    从状态机，用于分析出一行的内容
    返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
    m_read_idx指向缓冲区m_read_buf的数据末尾的下一个字节
    m_checked_idx指向从状态机当前正在分析的字节
*/
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(;m_checked_idx < m_read_idx;++m_checked_idx)
    {
        /* temp为将要分析的字节 */
        temp = m_read_buf[m_checked_idx];

        /* 如果当前是\r字符，则有可能会读取到完整行 */
        if ( temp == '\r' ){
            /* 下一个字符到达buffer结尾，则接收不完整，需要继续接收 */
            if ( (m_checked_idx+1) == m_read_idx )
                return LINE_OPEN;
            /* 下一个字符是\n，将\r\n改为\0\0 */
            else if ( m_read_buf[m_checked_idx+1] == '\n'){
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            /* 如果都不符合，则语法错误 */
            return LINE_BAD;
        }
        /*
            如果当前字符是\n,也有可能读取到完整行
            由于上次读取到的\r就到了buffer末尾，没有接收完整，再次接收会出现这种情况
        */
       else if ( temp == '\n' )
       {
           /* 前一个字符是\r,则接收完整 */
           if ( m_checked_idx > 1 && m_read_buf[m_checked_idx-1] == '\r')
           {
               m_read_buf[m_checked_idx-1] = '\0';
               m_read_buf[m_checked_idx++] = '\0';
               return LINE_OK;
           }
           return LINE_BAD;
       }
    }
    /* 并没有找到\r\n，需要继续接收 */
    return LINE_OPEN;
}

void http_conn::process()
{
    /* 工作线程运行 */
    HTTP_CODE read_ret = process_read();
    //NO_REQUEST，表示请求不完整，需要继续接收请求数据
    if ( read_ret == NO_REQUEST )
    {
        /* 注册监听事件，注册epolloneshot事件，因为上述请求不完整，需要继续接收 */
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    /* 调用process_write完成报文响应 */
    bool write_ret = process_write(read_ret);
    if (!write_ret)
        close_conn();
    /* 注册并监听退出事件 */
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}

/* 读 */
http_conn::HTTP_CODE http_conn::process_read()
{
    //初始化从状态机状态、HTTP请求解析结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    /*
        在GET请求报文中，每一行都是\r\n作为结束，所以对报文进行拆解时，
        仅用从状态机的状态line_status=parse_line())==LINE_OK语句即可。

        但，在POST请求报文中，消息体的末尾没有任何字符，
        所以不能使用从状态机的状态，这里转而使用主状态机的状态作为循环入口条件。
    */
   while ( (m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK) )
   {
       text = get_line();
       m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
            ret = parse_request_line(text);
            if ( ret == BAD_REQUEST )
                return BAD_REQUEST;
            break;
        case CHECK_STATE_HEADER:
            ret= parse_headers(text);
            if ( ret == BAD_REQUEST )
                return BAD_REQUEST;
            /* 若为get，没有消息报文 */
            else if ( ret == GET_REQUEST )
                return do_respones();
            break;
        case CHECK_STATE_CONTENT:
            ret = parse_content(text);
            /* 获得完整HTTP请求 */
            if ( ret == GET_REQUEST )
                return do_respones();
            line_status = LINE_OPEN;
            break;
        default:
            return INTERNAL_ERROR;
            break;
        }
   }
    return NO_REQUEST;
}

/* 解析http请求行，获得请求方法，目标url以及http版本号 */
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    /*
        在HTTP报文中，请求行用来说明请求类型，要访问的资源以及所使用的HTTP版本，其中各个部分之间通过\t或空格分隔
        请求行中最先含有空格和\t任一字符的位置并返回
    */
    m_url = strpbrk(text, " \t");

   /* 如果没有空格或\t，则报文格式有误 */
    if ( !m_url )
        return BAD_REQUEST;
    
    /* 将该位置改为\0，用于前面数据取出 */
    *m_url++ = '\0';

    /* 取出数据，并通过与GET和POST比较，以确定请求方法 */
    char *method = text;
    if ( strcasecmp(method, "GET") == 0 )
        m_method = GET;
    else if ( strcasecmp(method, "POST") == 0 )
    {
        m_method = POST;
        cgi = 1;
    }
    /* 其它未做处理 */
    else
        return BAD_REQUEST;

    /* 从m_url此时跳过第一个空格或\t字符，但不知道之后是否还有 */
    /* 将m_url向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符 */
    /* 用于跳过空格和\t字符 */
    m_url += strspn(m_url, " \t");

    /* 使用与判断请求方法的相同逻辑，判断HTTP版本号 */
    m_version = strpbrk(m_url, " \t");
    if ( !m_version )
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    /* 仅支持HTTP/1.1 */
    if ( strcasecmp(m_version, "HTTP/1.1") != 0 )
        return BAD_REQUEST;

    /* 对请求资源前7个字符进行判断 */
    /* 这里主要是有些报文的请求资源会带有http://，这里需要对这种情况单独处理 */
    if ( strncasecmp(m_url, "http://", 7) == 0 )
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    /* 同样增加https情况 */
    if ( strncasecmp(m_url, "https://", 8) == 0 )
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    /* 一般的不会带有上述两种富豪，直接是单独的/或者/后面带访问资源 */
    if ( !m_url || m_url[0] != '/' )
        return BAD_REQUEST;

    /* 当url为/时，显示 欢迎界面 */
    if ( strlen(m_url) == 1 )
        strcat(m_url, "judge.html");

    /* 请求行处理完毕，将主状态机转移处理请求头 */
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/* 解析HTTP请求的一个头部信息 */
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    /* 判断是空行还是请求头 */
    if ( text[0] == '\0' )
    {
        /* 判断是GET还是POST */
        if ( m_content_length != 0 )
        {
            /* POST需要跳转到消息体处理状态 */
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        /* 如果读取到空行，且method为GET则报文读取完毕 */
        return GET_REQUEST;
    }
    /* 解析请求头部连接字段 */
    else if ( strncasecmp(text, "Connection:", 11 ) == 0 )
    {
        text += 11;

        /* 跳过空格和\t字符 */
        text += strspn(text, " \t");
        if ( strcasecmp(text, "keep-alive") == 0 )
        {
            /* 如果是长连接，则将linger标志设置为true */
            m_linger = true;
        }
    }
    /* 解析请求头部内容长度字段 */
    else if ( strncasecmp( text,"Content-length:",15 ) == 0 )
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    /* 解析请求头部HOST字段 */
    else if ( strncasecmp(text, "Host:", 5) == 0 )
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else{
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

/* 判断http请求是否被完全读入 */
http_conn::HTTP_CODE http_conn::parse_content( char *text )
{
    if ( m_read_idx >= (m_content_length + m_checked_idx) )
    {
        text[m_content_length] = '\0';
        /* POST请求中最后输入为用户名和密码 */
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}


/*
    / ：GET请求，跳转到judge.html
    /0 ：POST请求，跳转到register.html，即注册页面

*/

http_conn::HTTP_CODE http_conn::do_respones()
{
    /* 将初始化的m_real_file赋值为网站的根目录 */
    strcpy(m_real_file, doc_root);
    int len = strlen( doc_root );
    const char *p = strchr(m_url, '/');

    /* 处理CGI,实现登录注册 */
    if ( cgi == 1 && ( *(p+1) == '2' || *(p+1) == '3' ))
    {
        /* 根据标志判断是登录注册还是注册检测 */
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        /* 将用户名和密码提取出来 */
        /* user=123&&passwd=123 */
        char name[100], passwd[100];
        int i;
        for ( i = 5; m_string[i] != '&' ; i++) {
            name[i - 5] = m_string[i];
        }
        name[i-5] = '\0';

        int j = 0;
        for ( i = i + 10; m_string[i] != '\0'; ++j, ++i ){
            passwd[j] = m_string[i];
        }   
        passwd[j] = '\0';

        if ( *(p+1) == '3' ){
            /* 如果是注册，先检验数据中是否有重名的 */
            /* 没有重名的，进行增加数据 */
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username,passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, passwd);
            strcat(sql_insert, "')");
            if ( users.find(name) == users.end() ){
                m_lock.lock();
                int res = mysql_query( mysql, sql_insert );
                users.insert(pair<string, string>(name, passwd));
                m_lock.unlock();
                if ( !res )
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        /* 
            如果是登录，直接判断 
            若浏览器输入的用户名和密码在表中可以查到，返回1，否则返回0
        */
       else if ( *(p+1) == '2' )
       {
            if ( users.find(name) != users.end() && users[name] == passwd )
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
       }

    }
    /* 如果请求资源为/0,表示跳转注册界面 */
    if ( *(p+1) == '0' ){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if ( *(p+1) == '1' ){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if ( *(p+1) == '5' ){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if ( *(p+1) == '6' ){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if ( *(p+1) == '7' ){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    /*
        通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
        失败则返回NO_RESOURCE状态，表示资源不存在
    */
    if ( stat(m_real_file, &m_file_stat) < 0 )
        return NO_RESOURCE;
    /* 判断文件的权限，是否可读，不可读返回FORBIDDEN_REQUEST */
    if ( !(m_file_stat.st_mode & S_IROTH) )
        return FORBIDDEN_REQUEST;
    /* 判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误 */
    if ( S_ISDIR(m_file_stat.st_mode) )
        return BAD_REQUEST;

    /* 以只读方式获取文件描述符，通过mmap将该文件映射到内存中 */
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE,fd ,0);
    /* 避免文件描述符的浪费和占用 */
    close(fd);

    return FILE_REQUEST;
}

/* 写 */
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        /* 内部错误 */
        case INTERNAL_ERROR:
            /* 状态行 */
            add_status_line(500, error_500_title);
            /* 消息报头 */
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))
                return false;
            break;
        /* 报文语法有错误，404 */
        case BAD_REQUEST:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if ( !add_content(error_404_form) )
                return false;
            break;
        /* 资源没有访问权限，403 */
        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form))
                return false;
            break;
        /* 文件存在，200 */
        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            /* 如果请求的资源存在 */
            if ( m_file_stat.st_size != 0 )
            {
                add_headers(m_file_stat.st_size);
                /* 第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx */
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                /* 第二个iovec指针指向mmap返回的文件指针、长度指向文件大小 */
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                /* 发送的全部数据为响应报文头部信息和文件大小 */
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                LOG_INFO("buf = %s", m_write_buf);
                return true;
            }
            else
            {
                /* 如果请求的资源大小为0，则返回空白的HTML文件 */
                const char* ok_string="<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string))
                        return false;
            }
        default:
            break;
    }
    /* 除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区 */
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

/* 内部调用add_respones函数更新m_write_idx指针和缓冲区m_write_buf */
bool http_conn::add_response(const char *format, ...)
{
    /* 如果写入内容超出m_write_buf大小则报错 */
    if ( m_write_idx >= WRITE_BUFFER_SIZE )
        return false;
    /* 定义可变参数列表 */
    va_list arg_list;
    /* 将变量arg_list初始化为传入参数 */
    va_start(arg_list, format);
    /* 将数据format从可变参数写入缓冲区中，返回写入数据的长度 */
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);

    /* 如果写入的数据长度超过缓冲区剩余空间，则报错 */
    if ( len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx ) ){
        va_end(arg_list);
        return false;
    }
    /* 更新m_write_idx位置 */
    m_write_idx += len;
    /* 清空可变参数列表 */
    va_end(arg_list);
    LOG_INFO("request:%s", m_write_buf);
    return true;
}


/* 添加状态行 */
bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}


/* 添加消息报头，具体的添加文本长度、连接状态和空行 */
bool http_conn::add_headers( int content_len )
{
    return add_content_length( content_len ) && add_linger() && add_blank_line();
}

/* 添加Content-length，表示响应报文的长度 */
bool http_conn::add_content_length( int content_len )
{
    return add_response("Content-Length:%d\r\n", content_len);
}

/* 添加文本类型，这里是html */
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

/* 添加连接状态，通知浏览器是保持连接还是关闭 */
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger==true)?"keep-alive":"close");
}

/* 添加空行 */
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

/* 添加文本content */
bool http_conn::add_content( const char* content )
{
    return add_response("%s", content);
}


/* 发送响应报文 */
bool http_conn::write()
{
    int temp = 0;
    
    if ( bytes_to_send == 0 )
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }
    while(1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if ( temp < 0 )
        {
            if ( errno == EAGAIN )
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode );
                return true;
            }
            unmap();
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        if ( bytes_have_send >= m_iv[0].iov_len )
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
        if ( bytes_to_send <= 0 )
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode );
            if (m_linger)
            {
                init();
                return true;
            }
            else
                return false;
        }
    }
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
