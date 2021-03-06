////////////////////////////////////////////////
// Name: Socket.h
// Author: jiang.mingjun
// Date: 2010/12/2
// Description: Socket 封装类
////////////////////////////////////////////////
#ifndef SOCKET_H_HEADER_INCLUDED_BE6E2CF5
#define SOCKET_H_HEADER_INCLUDED_BE6E2CF5

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#else
#include <process.h>
  #include <windows.h>
  #include <tlhelp32.h>
  #include <winsock.h>
#endif
#include "Helper/mdbPerfInfo.h"

//namespace QuickMDB{


    //## TCP/IP Socket封装
    class Socket
    {
    public:
        /******************************************************************************
        * 函数名称	:  Create()
        * 函数描述	:  创建socket   
        * 输入		:  无
        * 输出		:  无
        * 返回值	:  成功返回0，否则返回<0
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        int Create();

        /******************************************************************************
        * 函数名称	:  Bind()
        * 函数描述	:  绑定端口   
        * 输入		:  iPortID, 端口  
        * 输出		:  无
        * 返回值	:  成功返回0，否则返回<0
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        int  Bind(int iPortID);

        /******************************************************************************
        * 函数名称	:  Listen()
        * 函数描述	:  服务器侦听(服务端使用) 
        * 输入		:  无  
        * 输出		:  无
        * 返回值	:  成功返回0，否则返回<0
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        int Listen();

        /******************************************************************************
        * 函数名称	:  Accept()
        * 函数描述	:  获取连接请求  
        * 输入		:  无 
        * 输出		:  无
        * 返回值	:  成功返回0/1(不存在创建则为0,存在返回则为1)，否则返回<0
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        int Accept();

        /******************************************************************************
        * 函数名称	:  connect()
        * 函数描述	:  与TCP服务器的连接  
        * 输入		:  sRemoteHostIP, 服务端IP地址 
        * 输入		:  iRemotePortID, 服务端监听端口，iTimeout 超时时间
        * 输出		:  无
        * 返回值	:  成功返回socket连接句柄,否则返回<0
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        int connect(char *sRemoteHostIP, int iRemotePortID,int iTimeout);

        /******************************************************************************
        * 函数名称	:  Select()
        * 函数描述	:  监视某个或某些句柄的状态变化，获取可读句柄    
        * 输入		:  无 
        * 输出		:  无
        * 返回值	:  成功返回0/1(不存在创建则为0,存在返回则为1)，否则返回<0
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        int Select();

        /******************************************************************************
        * 函数名称	:  FdIsSet()
        * 函数描述	:  判断某个socket是否在fd_set类型的集合中  
        * 输入		:  无
        * 输出		:  无
        * 返回值	:  true 在，false 不在
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        bool FdIsSet();

        /******************************************************************************
        * 函数名称	:  SetBlock()
        * 函数描述	:  设置本套接口的非阻塞标志
        * 输入		:  iSocketID, socket句柄
        * 输出		:  无
        * 返回值	:  返回0 ：成功  -1 ：出错
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        int SetBlock(int iSocketID);

        /******************************************************************************
        * 函数名称	:  Close()
        * 函数描述	:  关闭socket   
        * 输入		:  无  
        * 输出		:  无
        * 返回值	:  无
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        void Close();

        /******************************************************************************
        * 函数名称	:  CloseListenSocket()
        * 函数描述	:  关闭监控的socket   
        * 输入		:  无 
        * 输出		:  无
        * 返回值	:  无
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        void CloseListenSocket();
        Socket();
        /******************************************************************************
        * 函数名称	:  SetSocketID()
        * 函数描述	:  设置socket   
        * 输入		:  iSocketID, socket句柄  
        * 输出		:  无
        * 返回值	:  无
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        void SetSocketID(int iSocketID);

        virtual ~Socket();

        /******************************************************************************
        * 函数名称	:  read()
        * 函数描述	:  读socket数据，若长度低于iLen，则继续等待接收。CSP协议用
        * 输入		:  iLen, 读取字节数  
        * 输出		:  sBuffer, 读取的数据
        * 返回值	:  返回结果>0 表示读取的字节数，否则返回失败码
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        int read(unsigned char *sBuffer, int iLen);
        int read(int iSocket,char *sBuffer, int iLen);

        /******************************************************************************
        * 函数名称	:  Recv()
        * 函数描述	:  读socket，通用接口
        * 输入		:  iLen, buffer长度
        * 输出		:  sBuffer, 缓冲区，用来存放读取的数据
        * 返回值	:  返回结果>0 表示读取的字节数，否则返回失败码
        * 作者		:  zhang.lin
        *******************************************************************************/
        int Recv(unsigned char *sBuffer, int iLen);

        /******************************************************************************
        * 函数名称	:  write()
        * 函数描述	:  写socket,发送socket数据，若长度低于iLen，则继续等待发送。CSP协议用
        * 输入		:  sBuffer 发送内容，iLen 发送字节长度
        * 输出		:  无
        * 返回值	:  实际发送的字节数
        * 作者		:  jiang.mingjun
        *******************************************************************************/
        int write(unsigned char *sBuffer, int iLen);

        /******************************************************************************
        * 函数名称	:  Send()
        * 函数描述	:  发送长度为iLen的数据，通用接口
        * 输入		:  sBuffer 发送内容，iLen 发送字节长度
        * 输出		:  无
        * 返回值	:  实际发送的字节数,失败返回错误码
        * 作者		:  zhang.lin
        *******************************************************************************/
        int Send(unsigned char *sBuffer, int iLen);

    public:
        struct sockaddr_in  m_tAddr, m_tAddTmp;   
        
    private:
        int m_iSocketID;
        int m_iListenSocketID;
        fd_set m_rSet;
    };

//}    

#endif /* SOCKET_H_HEADER_INCLUDED_BE6E2CF5 */
