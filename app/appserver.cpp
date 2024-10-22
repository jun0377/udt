#ifndef WIN32
   #include <unistd.h>
   #include <cstdlib>
   #include <cstring>
   #include <netdb.h>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #include <wspiapi.h>
#endif
#include <iostream>
#include <udt.h>
#include "cc.h"
#include "test_util.h"

using namespace std;

#ifndef WIN32
void* recvdata(void*);
#else
DWORD WINAPI recvdata(LPVOID);
#endif

int main(int argc, char* argv[])
{
   if ((1 != argc) && ((2 != argc) || (0 == atoi(argv[1]))))
   {
      cout << "usage: appserver [server_port]" << endl;
      return 0;
   }

   // Automatically start up and clean up UDT module.
   // 自动管理udt实例，类似C++11中std::lock_guard
   UDTUpDown _udt_;

   addrinfo hints;
   addrinfo* res;

   memset(&hints, 0, sizeof(struct addrinfo));

   hints.ai_flags = AI_PASSIVE;        // 当设置了AI_PASSIVE时，返回的套接口地址为INADDR_ANY
   hints.ai_family = AF_INET;          // IPV4
   hints.ai_socktype = SOCK_STREAM;    // TCP
   //hints.ai_socktype = SOCK_DGRAM;

   // 服务器监听的端口,可通过命令行传入
   string service("9000");
   if (2 == argc)
      service = argv[1];

   std::cout << "[main] listen port: " << service << std::endl;

   // 获取用于创建套接字的地址信息,使用这个函数，当端口号超出65535时，实际使用的端口号将是对65536取模后的值
   if (0 != getaddrinfo(NULL, service.c_str(), &hints, &res))
   {
      cout << "[main] illegal port number or port is busy.\n" << endl;
      return 0;
   }

   // socket,这一步实际上并没有执行系统socket函数，只是初始化了一些参数
   UDTSOCKET serv = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
   std::cout << "[main] sockfd: " << serv << std::endl;

   // UDT Options
   //UDT::setsockopt(serv, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
   //UDT::setsockopt(serv, 0, UDT_MSS, new int(9000), sizeof(int));
   //UDT::setsockopt(serv, 0, UDT_RCVBUF, new int(10000000), sizeof(int));
   //UDT::setsockopt(serv, 0, UDP_RCVBUF, new int(10000000), sizeof(int));

   // bind
   if (UDT::ERROR == UDT::bind(serv, res->ai_addr, res->ai_addrlen))
   {
      cout << "[main] bind: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }
   
   // 和getaddrinfo搭配使用，释放内存，否则会造成内存泄露
   freeaddrinfo(res);

   cout << "[main] server is ready at port: " << service << endl;

   // listen
   if (UDT::ERROR == UDT::listen(serv, 10))
   {
      cout << "[main] listen: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   // client地址
   sockaddr_storage clientaddr;
   int addrlen = sizeof(clientaddr);

   UDTSOCKET recver;

   while (true)
   {
      // 阻塞接收连接
      if (UDT::INVALID_SOCK == (recver = UDT::accept(serv, (sockaddr*)&clientaddr, &addrlen)))
      {
         cout << "[main] accept: " << UDT::getlasterror().getErrorMessage() << endl;
         return 0;
      }

      char clienthost[NI_MAXHOST];
      char clientservice[NI_MAXSERV];

      // 对端IP、端口
      getnameinfo((sockaddr *)&clientaddr, addrlen, clienthost, sizeof(clienthost), clientservice, sizeof(clientservice), NI_NUMERICHOST|NI_NUMERICSERV);
      cout << "[main] peer addr: " << clienthost << ":" << clientservice << endl;

      // 接收客户端数据的线程
#ifndef WIN32
         pthread_t rcvthread;
         pthread_create(&rcvthread, NULL, recvdata, new UDTSOCKET(recver));
         pthread_detach(rcvthread);
#else
         CreateThread(NULL, 0, recvdata, new UDTSOCKET(recver), 0, NULL);
#endif
   }

   UDT::close(serv);

   return 0;
}

#ifndef WIN32
void* recvdata(void* usocket)
#else
DWORD WINAPI recvdata(LPVOID usocket)
#endif
{
   // 释放线程参数占用的堆空间
   UDTSOCKET recver = *(UDTSOCKET*)usocket;
   delete (UDTSOCKET*)usocket;

   char* data;
   int size = 100000;
   data = new char[size];

   while (true)
   {
      int rsize = 0;
      int rs;
      while (rsize < size)
      {
         int rcv_size;
         int var_size = sizeof(int);
         // 获取UDT接收缓冲区大小
         UDT::getsockopt(recver, 0, UDT_RCVDATA, &rcv_size, &var_size);
         if (UDT::ERROR == (rs = UDT::recv(recver, data + rsize, size - rsize, 0)))
         {
            cout << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
            break;
         }

         rsize += rs;
      }

      if (rsize < size)
         break;
   }

   delete [] data;

   UDT::close(recver);

   #ifndef WIN32
      return NULL;
   #else
      return 0;
   #endif
}
