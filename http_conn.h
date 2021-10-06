#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <stdarg.h>
#include "threadpool.h"
#include "locker.h"
#include "sem.h"
#include "cond.h"
#include "utill_timer.h"
       
class http_conn {
public:
    static const int FILENAME_LEN = 200;//文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;//读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 2048;//写缓冲区的大小

    /*******HTTP请求方法**********/
    //定义成枚举类型，这里支持GET,自己补充实现POST请求
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    /************主状态机的状态**************/
    /*
    *   解析客户端请求时候，主状态机的状态主要分为以下三个部分：
    *   1.解析请求行，
    *   2.解析请求头
    *   3.解析请求体（一般不用在请求时候）
    */
    //定义解析HTTP协议的哪个部分的状态标识
    enum CHECK_STATE {CHEACK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};


    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST : 请求不完整
        GTE_REQUEST : 表示获得了一个完成的客户请求
        BAD_REQUEST : 表示客户请求语法错误
        NO_RESOURCE : 表示没有服务器资源
        FORBIDDEN_REQUEST : 表示客户对资源没有足够的访问权限
        FILE_REQUEST : 文件请求，获取文件成功
        INTERNAL_ERROR : 表示服务器内部错误
        CLOSED_CONNECTION : 表示客户端已经关闭连接了
    */
    enum HTTP_CODE {NO_REQUEST = 0, GET_REQUEST, BAD_REQUEST, NO_RESOURCE,
                    FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    /*
        定义有限状态机
        状态机的状态有三种可能，即行的读取状态，分别表示：
        1.读取到完整的行；2.行出错；3.行数据尚且不完整
    */
    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};

public:
    http_conn(){}//构造函数
    ~http_conn(){}//析构函数

    void initNewConn(int sockfd, const sockaddr_in& addr);//初始化新接入的连接
    void closeConn();//关闭连接
    void process();//处理客户端的请求
    bool readRequest();//非阻塞读取客户端发来的请求
    bool writetoClient();//非阻塞写，给客户端回写数据
    const sockaddr_in getClientAddr();

    // void cb_func (int);
    // //处理时间事件
    // void deal_timer ();

private:
    void init();//初始化连接

    //解析HTTP请求，主状态机解析，先解析请求行，在解析请求头，在解析请求体
    HTTP_CODE process_read();
    //填充HTTP应答
    bool process_write(HTTP_CODE ret);

    //下面一组函数被process_read用来解析HTTP协议
    HTTP_CODE parse_request_line(char* text);//解析请求行
    HTTP_CODE parse_headers(char* text);//解析请求头
    HTTP_CODE parse_content(char* text);//解析请求体
    char* get_line(){return m_read_buf + m_start_line;}//获取一行字符串
    LINE_STATUS parse_line();//解析行
    HTTP_CODE do_request();//解析完HTTP请求报文以后，做出响应

    //下面这一组函数被process_write用来调用以填充HTTP应答
    void unmap();//对内存映射区执行munmap操作
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_content_type();
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();


public:
    static int m_epollfd;//所有的工作都要共享一个epoll内存，一起监听
    static int m_user_count;//当前所有用户的数量

    
private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];//读缓冲区
    int m_read_idx;//标识读缓冲区中已经读入数据的下一个位置
    int m_checked_idx;//当前正在分析的字符在读缓冲区的位置
    int m_start_line;//当前正在解析的行的起始位置

    CHECK_STATE m_check_state;//主状态机当前所处的状态
    METHOD m_method;//请求方法

    char m_real_file[FILENAME_LEN];//客户请求的目标文件的完整路径，其内容等于doc_root + m_url, doc_root是网站的根目录
    char* m_url;//客户请求的目标文件的文件名
    char* m_version;//HTTP协议版本号，我们仅仅支持HTTP1.1
    char* m_host;//主机名
    int m_content_length;//HTTP请求的消息总长度
    bool m_linger;//HTTP请求是否要求保持连接

    char m_write_buf[WRITE_BUFFER_SIZE];//写缓冲区
    int m_write_idx;//写缓冲中待发送的字节数
    char* m_file_address;//客户请求的目标文件被mmap到内存中
    struct stat m_file_stat;//目标文件的状态，通过它我们可以判断文件是否存在、是否为目录、是否可读、并获取文件大小等信息,通过文件名filename获取文件信息，并保存在buf所指的结构体stat中
    struct iovec m_iv[2];//我们采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写的内存块数量
    int m_iv_count;

    int bytes_to_send;              // 将要发送的数据的字节数
    int bytes_have_send;            // 已经发送的字节数

    utill_timer* m_timer;//定时器
};

#endif