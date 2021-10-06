#include "http_conn.h"
#include "sort_timer_list.h"
#include "_freecplus.h"

#define TIMESLOT 5

extern CLogFile logfile;
static sort_timer_list timer_lst;

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

//网站根目录
const char* doc_root = "/root/Lcs/network/mynetwork/buildwebsever/resources";

//将文件描述符设置为非阻塞
int setNoBlock (int fd) {
    int old_opt = fcntl(fd, F_GETFL); //获取原文件描述符的状态
    int new_opt = old_opt | O_NONBLOCK;//将原文件描述符添加非阻塞
    fcntl(fd, F_SETFL, new_opt);// 重新设置文件描述符状态为非阻塞
    return old_opt;
}

/*
    即使可以使用 ET 模式，一个socket 上的某个事件还是可能被触发多次。这在并发程序中就会引起一个
问题。比如一个线程在读取完某个 socket 上的数据后开始处理这些数据，而在数据的处理过程中该
socket 上又有新数据可读（EPOLLIN 再次被触发），此时另外一个线程被唤醒来读取这些新的数据。于
是就出现了两个线程同时操作一个 socket 的局面。一个socket连接在任一时刻都只被一个线程处理，可
以使用 epoll 的 EPOLLONESHOT 事件实现。
对于注册了 EPOLLONESHOT 事件的文件描述符，操作系统最多触发其上注册的一个可读、可写或者异
常事件，且只触发一次，除非我们使用 epoll_ctl 函数重置该文件描述符上注册的 EPOLLONESHOT 事
件。这样，当一个线程在处理某个 socket 时，其他线程是不可能有机会操作该 socket 的。但反过来思
考，注册了 EPOLLONESHOT 事件的 socket 一旦被某个线程处理完毕， 该线程就应该立即重置这个
socket 上的 EPOLLONESHOT 事件，以确保这个 socket 下一次可读时，其 EPOLLIN 事件能被触发，进
而让其他工作线程有机会继续处理这个 socket。
*/
//添加文件描述符
void adfd (int epollfd, int fd, bool oneshoot) {//第三个参数的意义如上所述
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;    // 监控读事件和连接关闭
    if (oneshoot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);//向内核中的epollfd中添加要监听的文件描述符
    /*
        这里需要将文件描述符设置为非阻塞状态才能实现epoll的高效使用，为什么呢？原理还不太清楚，后面查一下，一下子没找到！！！！
    */
    setNoBlock(fd);//将文件描述符设置为非阻塞状态
}
//移除文件描述符
void removefd (int epollfd, int fd) {//移除需要监听的文件描述符

    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);//删除文件描述符
    close(fd);//关闭文件描述符
}
//修改文件描述符
void modfd (int epollfd, int fd, int ev) {//修改文件描述符
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;//添加新事件
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);//修改事件属性
}

//初始化客户数量
int http_conn::m_user_count = 0;//开始时候为0,类外初始化
//初始化socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态
int http_conn::m_epollfd = -1;//初始化内核事件文件描述符

//关闭连接
void http_conn::closeConn () {
    if (m_sockfd != -1) {//这个工作的通信套接字
        removefd(m_epollfd, m_sockfd);//从共享的内核事件文件描述符中移除当前通信描述符
        utill_timer* timer = m_timer;
        m_timer = nullptr;
        if (!timer_lst.isEmpty()) {
            timer_lst.del_timer(timer);
        }  
        m_sockfd = -1;//将通信套接字设置为-1，表示无通信描述符占用
        --m_user_count;//关闭一个连接当然通信描述符-1
    }
}

void timer_handler () {
    //定时处理任务，实际上就是调用tick()函数
    timer_lst.tick();
}


//处理时间事件
void deal_timer () {
    timer_handler();
}

//回调函数
void cb_func (int fd) {
    if (fd != -1) {//这个工作的通信套接字
        removefd(http_conn::m_epollfd, fd);//从共享的内核事件文件描述符中移除当前通信描述符
        fd = -1;//将通信套接字设置为-1，表示无通信描述符占用
        --http_conn::m_user_count;//关闭一个连接当然通信描述符-1
    }
}


//初始化连接，外部调用初始化套接字地址
 void http_conn::initNewConn(int sockfd, const sockaddr_in& addr){//初始化新接入的连接
    m_sockfd = sockfd;
    m_address = addr;
    ++m_user_count;
    //对这个套接字设置端口复用
    int opt = 1;
    int res = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    if (res == -1) {//出错
        m_sockfd = -1;
        --m_user_count;
        //地址也应该清空，但是不知道如何清空
        // throw std::exception();
    }
    adfd(m_epollfd, sockfd, true);//将这个与客户通信的套接字加入内核epollfd中

    //创建定时器，设置回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器插入链表
    utill_timer* timer = new utill_timer;
    time_t cur = time(nullptr);
    timer->m_expire = cur + 3 * TIMESLOT;
    m_timer = timer;
    timer->m_user_sockfd = m_sockfd;
    timer->cb_func = cb_func;
    timer_lst.add_timer(timer);
    init();//对刚加入的客户进行初始化
 }

//初始化连接
 void http_conn::init () {
    bytes_have_send = 0;
    bytes_to_send = 0;

    m_check_state = CHEACK_STATE_REQUESTLINE;//主状态机的初始状态是检查请求行
    m_linger = false;//默认不保持连接

    m_method = GET;//默认请求方法为请求
    m_url = 0;//默认请求文件名
    m_version = 0;//默认HTTP版本协议
    m_content_length = 0;//默认请求消息的长度
    m_host = 0;//主机名
    m_start_line = 0;//当前正在解析行的起始位置
    m_checked_idx = 0;//当前正在分析的字符在读缓冲区的位置
    m_read_idx = 0;//标识下一个要读取的位置
    m_write_idx = 0;//表示下一个待发送数据的位置
    //初始化相关数组
    bzero(m_read_buf, READ_BUFFER_SIZE);//初始化读取数组
    bzero(m_write_buf, WRITE_BUFFER_SIZE);//初始化写缓冲的数组
    bzero(m_real_file, FILENAME_LEN);//初始化请求路径数组
}


// 浏览器请求数据
// GET / HTTP/1.1
// Host: 39.101.193.226:5005
// User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.116 Safari/537.36
// Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9
// Accept-Encoding: gzip, deflate
// Accept-Language: zh-CN,zh-TW;q=0.9,zh;q=0.8,en-US;q=0.7,en;q=0.6
// Cache-Control: max-age=0
// Upgrade-Insecure-Requests: 1

//循环读取客户端数据，直到无数据可读或者对方关闭连接，调用完这个函数，数据已经被读取到read_buf中然后进行解析就行了
 bool http_conn::readRequest () {
    //std::cout << "一次性读取数据" << std::endl;
    if(m_read_idx >= READ_BUFFER_SIZE) {//读取缓冲区已满，无法继续读取
        return false;
    }
    int byte_read = 0;
    while (true) {//循环读取
        //从m_read_buf中读取数据
        byte_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (byte_read == -1) {//发生错误
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;//没有数据可读
            }
            return false;
        }
        else if (byte_read == 0) {//对方关闭连接
            return false;
        }
        m_read_idx += byte_read;//转移值下一次可读
    }
    // printf("%s\n", m_read_buf);
    // std:: cout << *m_read_buf << std::endl;
    //如果客户端上有数据可读，则我们需要调整该连接对应的定时器，以延迟到期
    if (m_timer != nullptr) {
        time_t cur = time(nullptr);
        m_timer->m_expire = cur + 3 * TIMESLOT;
        std::cout << "adjust timer once：" << m_sockfd << std::endl;
        timer_lst.adjust_timer(m_timer);
    }
    return true;//读数据成功
 }

/*********火狐浏览器访问baidu网站的请求报文***********/
/*
    GET / HTTP/1.1
    Host: www.baidu.com
    User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:78.0) Gecko/20100101 Firefox/78.0
    Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*;q=0.8
    Accept-Language: zh-CN,zh;q=0.8,zh-TW;q=0.7,zh-HK;q=0.5,en-US;q=0.3,en;q=0.2
    Accept-Encoding: gzip, deflate, br
    Connection: keep-alive
    Cookie: BAIDUID=7409B7499AFB480F1D3DB99911624265:FG=1; BIDUPSID=7409B7499AFB480F3FD287EAF7E7A722; PSTM=1623056238; BD_UPN=133352
    Upgrade-Insecure-Requests: 1
*/
//解析一行，判断依据是末尾\r\n
http_conn::LINE_STATUS http_conn::parse_line() {//解析行
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];//读缓冲区的一行，根据http协议请求报文看
        if (temp == '\r') {//空格间隔
            if ((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;//行数据尚且不完整
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';//变成字符串结束标志， GET / HTTP/1.1\t\n-> GET / HTTP/1.1\0\0
                m_read_buf[m_checked_idx++] = '\0';//变成字符串结束标志，这个自增调到下一个要解析的位置
                return LINE_OK;//行读取完整
            }
            return LINE_BAD;
        }
        else if (temp == '\n') {//向前检查
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx-1] = '\0';//变成字符串结束标志
                m_read_buf[m_checked_idx++] = '\0';//变成字符串结束标志
                return LINE_OK;
            }
            else {
                return LINE_BAD;
            }
        }
    }
    return LINE_OPEN;
}
/***下面一组函数被process_read用来解析HTTP协议**/
//解析请求行
http_conn::HTTP_CODE http_conn::parse_request_line (char* text) {//解析请求行
    //std::cout << "开始解析HTTP请求，请求行解析中》》》" << std::endl;
    //请求行格式： GET / HTTP/1.1
    m_url = strpbrk(text, " \t");//查找在text中空格字符,返回查找到的位置
    if (!m_url) {//未查找到
        return BAD_REQUEST;
    }
    *m_url++ = '\0';//将空格设置为字符结束符GET\0/ HTTP/1.1,并将m_url指向下一个位置
    char* methond = text;//此时text是：GET\0/ HTTP/1.1,而指针指向的这是GET
    if (strcasecmp(methond, "GET") == 0) {//比较是否相等
        m_method = GET;//客户端的请求是GET;
    }
    else {//这里只支持GET方法
        return BAD_REQUEST;
    }
    //继续解析后面的数据
    //此时m_url的指向是指在/ HTTP/1.1，重复找到空格，设置\0,获取字符串
    m_version = strpbrk(m_url, " \t");//找到空格
    if (!m_version) {//未找到空格
        return BAD_REQUEST;
    }
    *m_version++ = '\0';//将空格设置为字符结束符/\0HTTP/1.1,并将m_url指向下一个位置
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {//是否是HTTP1.1
        return BAD_REQUEST;
    }
    //解析url
    /**
     * http://192.168.110.129:10000/index.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0) {//比较m_url前7个是否是这个
        m_url += 7;//移动到真正的ip地址处
        //在参数str所指向的字符串中搜索第一次出现字符c（一个无符号字符）的位置
        m_url = strchr(m_url, '/');
        //std:: cout << m_url << std::endl;
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER;//开始检查请求头
    return NO_REQUEST;//请求不完整
}
//解析请求头
http_conn::HTTP_CODE http_conn::parse_headers (char* text) {//解析请求头
    //遇到空行，表示请求头解析完毕
    if (text[0] == '\0') {
        //如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体
        //状态转移到CHECK_STATE_CONTENT状态
        if (m_content_length != 0) {//有请求体需要进行解析
            m_check_state = CHECK_STATE_CONTENT;//状态转移到解析请求体
            return NO_REQUEST;//请求不完整
        }
        //否则说明，没有请求体，我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0) {//判断连接情况
    /*
        Connection: keep-alive
    */
        //处理Connection头部字段，
        text += 11;//移动指针位置
        text += strspn(text, " \t");// 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
        if (strcasecmp(text, "keep-alive") == 0) {//判断是否是 保持连接
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0) {//解析请求体长度
        //处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);//转换为整数
    }
    else if (strncasecmp(text, "Host:", 5) == 0) {//获取Host头部字段,主机域名
        //处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;//主机域名
        // std:: cout << m_host << std::endl;
    }
    else {
       // std::cout << "oop! unkonw header" << text << std::endl;
    }
    return NO_REQUEST;//请求不完整，继续解析请求头
}
//解析请求体，在请求报文中一般不用这个字段，在响应报文中也可能没有这个字段
http_conn::HTTP_CODE http_conn::parse_content (char* text) {//解析请求体
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;//获取完整请求
    }
    return NO_REQUEST;//请求不完整，继续解析请求
}
//解析完HTTP请求报文以后，做出响应
/*
    当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性
    如果目标文件存在，对所有用户可读，并且不是目录，则使用mmap将其映射到内存
    地址为m_file_address处，并告诉调用者获取文件成功
*/
http_conn::HTTP_CODE http_conn::do_request () {
    //"/home/wenp/vscode/buildwebsever/resources"
    strcpy(m_real_file, doc_root);//拷贝到m_real_file
    int len = strlen(doc_root);//获取长度
    //strncpy(m_real_file + len, m_url, FILENAME_LEN - len -1);
    strcpy(m_real_file + len, m_url);//形成请求的完整路径：/home/wenp/vscode/buildwebsever/resources/index.html

    // printf("%s\n", m_real_file);
    //获取m_real_file文件的相关的状态信息，-1失败，0成功
    if (stat(m_real_file, &m_file_stat) < 0) {//函数stat()通过文件名filename获取文件信息，并保存在buf所指的结构体stat中
        return NO_REQUEST;//请求不完整
    }

    /*
        st_mode 宏定义中文件状态有如下这些：（部分）

        S_IROTH 00004             其他用户具可读取权限
        S_IWOTH 00002             其他用户具可写入权限
        S_IXOTH 00001             其他用户具可执行权限

        上述的文件类型在POSIX中定义了检查这些类型的宏定义：
        S_ISLNK (st_mode)    判断是否为符号连接
        S_ISREG (st_mode)    是否为一般文件
        S_ISDIR (st_mode)    是否为目录
        S_ISCHR (st_mode)    是否为字符装置文件
        S_ISBLK (s3e)        是否为先进先出
        S_ISSOCK (st_mode)   是否为socket
    */

    //判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH)) {//用户是否具有可读权限
        return FORBIDDEN_REQUEST;//禁止访问
    }

    //判断是否为目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;//访问错误
    }

    //以只读方式打开
    int fd = open(m_real_file, O_RDONLY);
    //创建内存映射
    m_file_address = (char*)mmap(nullptr, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}
//下面这一组函数被process_write用来调用以填充HTTP应答
void http_conn::unmap () {//对内存映射区执行munmap操作
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
//写HTTP响应
bool http_conn::writetoClient () {
    int temp = 0;
    if (bytes_to_send == 0) {
        //将要发送的字节树为0，这一次响应结束
        modfd(m_epollfd, m_sockfd, EPOLLIN);//改变文件描述符
        init();
        return true;
    }

    while (true) {//一直循环写，向客户端发送数据
        //分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            //如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间
            //服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();//写事件失败后，释放内存映射
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        
        if (m_iv[0].iov_len <= bytes_have_send) {//已经发送完HTTP响应报文
            //发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0) {
            //没有数据要发送了
            unmap();//释放内存映射
            modfd(m_epollfd, m_sockfd, EPOLLIN);//将文件描述符修改为读取状态

            if (m_linger) {//是否保持连接，是
                init();//重新初始化，准备下一次请求
                return true;
            }
            else {
                return false;
            }
        }
    }
}

//向写缓冲区中写入待发送的数据
bool http_conn::add_response(const char* format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {//写缓冲区以满
        return false;
    }

    va_list arg_list;//获取可变参数列表
    va_start(arg_list, format);//第一个参数是可变参数列表变量，第二个是可变参数前的最后一个确定变量参数，用来推算出可变参数的位置
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {//len最终成功写入的返回值数
        return false;
    }
    m_write_idx += len;//移动下一次需要开始写入的位置
    va_end( arg_list );//结束可变参数列表的写入过程

    return true;
}

bool http_conn::add_content(const char* content) {
    return add_response( "%s", content );
}

bool http_conn::add_content_type() {
    /*
    常见的媒体格式类型如下：
    text/html ： HTML格式
    text/plain ：纯文本格式      
    text/xml ：  XML格式
    image/gif ：gif图片格式    
    image/jpeg ：jpg图片格式 
    image/png：png图片格式
    以application开头的媒体格式类型：

    application/xhtml+xml ：XHTML格式
    application/xml     ： XML数据格式
    application/atom+xml  ：Atom XML聚合格式    
    application/json    ： JSON数据格式
    application/pdf       ：pdf格式  
    application/msword  ： Word文档格式
    application/octet-stream ： 二进制流数据（如常见的文件下载）
    */
    char* getTypeFile = strchr(m_url, '.');//找到位置
    ++getTypeFile;//移动到文件名真正开始的位置
    if (strcasecmp(getTypeFile, "pdf") == 0) {
        return add_response("Content-Type: %s\r\n", "application/pdf");
    }
    else if (strcasecmp(getTypeFile, "zip") == 0) {
        return add_response("Content-Type: %s\r\n", "application/octet-stream");
    }
    return add_response("Content-Type: %s\r\n", "text/html");
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(int content_length) {
    add_content_length(content_length);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_length) {
    return add_response( "Content-Length: %d\r\n", content_length);
}

bool http_conn::add_linger() {
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line() {
    return add_response( "%s", "\r\n" );
}

//主状态机：解析HTTP请求
http_conn::HTTP_CODE http_conn::process_read () {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    //std::cout << "开始解析HTTP请求，主状态机解析中》》》" << std::endl;
    //在解析请求体，并且行的状态也ok
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
            || ((line_status = parse_line()) == LINE_OK)) {
        //获取一行数据
        text = get_line();//获取一行数据
        m_start_line = m_checked_idx;

        //进行有限状态机
        switch (m_check_state) {
            case CHEACK_STATE_REQUESTLINE : {//解析请求行
                ret = parse_request_line(text);//解析请求行
                if (ret == BAD_REQUEST) {//解析失败，客户语法错误
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER : {//解析请求头
                ret = parse_headers(text);//解析请求体
                if (ret == BAD_REQUEST) {//解析失败
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST) {
                    return do_request();//开始执行
                }
                break;
            }
            case CHECK_STATE_CONTENT : {//解析请求体
                ret = parse_content(text);
                if (ret == GET_REQUEST) {//获得了一个完整的请求
                    return do_request();
                }
                break;
            }
            default : {
                return INTERNAL_ERROR;//错误
            }
        }
    }
    return NO_REQUEST;
}

//填充HTTP应答
bool http_conn::process_write (HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR : {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST : {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case NO_REQUEST : {
            add_status_line(404, error_400_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
        }
        case FORBIDDEN_REQUEST : {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
        }
        case FILE_REQUEST : {
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;

}

/************在工作线程中调用次函数*********************/
 void http_conn::process () {//工作线程需要执行的任务
    //解析客户端的HTTP请求
    // std::cout << "解析客户端的HTTP请求" << std::endl;
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {//没有请求
        modfd(m_epollfd, m_sockfd, EPOLLIN);// 修改socket状态，可再触发
        return;
    }

    //回复客户端的HTTP的请求
    bool write_ret = process_write(read_ret);
    if (!write_ret) {//写出失败，关闭连接
        closeConn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);//修改文件描述符有写事件已经准备好啦

 }










