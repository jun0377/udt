/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
****************************************************************************/

/****************************************************************************
written by
   Yunhong Gu, last updated 01/27/2011
*****************************************************************************/

#ifndef WIN32
   #include <netdb.h>
   #include <arpa/inet.h>
   #include <unistd.h>
   #include <fcntl.h>
   #include <cstring>
   #include <cstdio>
   #include <cerrno>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #ifdef LEGACY_WIN32
      #include <wspiapi.h>
   #endif
#endif
#include "channel.h"
#include "packet.h"

#ifdef WIN32
   #define socklen_t int
#endif

#ifndef WIN32
   #define NET_ERROR errno
#else
   #define NET_ERROR WSAGetLastError()
#endif


CChannel::CChannel():
m_iIPversion(AF_INET),
m_iSockAddrSize(sizeof(sockaddr_in)),
m_iSocket(),
m_iSndBufSize(65536),
m_iRcvBufSize(65536)
{
}

CChannel::CChannel(int version):
m_iIPversion(version),
m_iSocket(),
m_iSndBufSize(65536),
m_iRcvBufSize(65536)
{
   m_iSockAddrSize = (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
}

CChannel::~CChannel()
{
}

// 执行系统api: socket bind
void CChannel::open(const sockaddr* addr)
{
   // construct an socket
   // 调用系统API socket创建一个套接字
   m_iSocket = ::socket(m_iIPversion, SOCK_DGRAM, 0);

   #ifdef WIN32
      if (INVALID_SOCKET == m_iSocket)
   #else
      if (m_iSocket < 0)
   #endif
      throw CUDTException(1, 0, NET_ERROR);

   // 调用系统API bind绑定套接字
   if (NULL != addr)
   {
      // 获取地址长度，IPv6/IPv6的地址长度不同
      socklen_t namelen = m_iSockAddrSize;

      // 调用系统API bind到指定的地址上
      if (0 != ::bind(m_iSocket, addr, namelen))
         throw CUDTException(1, 3, NET_ERROR);
   }
   else
   {
      //sendto or WSASendTo will also automatically bind the socket
      addrinfo hints;
      addrinfo* res;

      memset(&hints, 0, sizeof(struct addrinfo));

      hints.ai_flags = AI_PASSIVE;
      hints.ai_family = m_iIPversion;
      hints.ai_socktype = SOCK_DGRAM;

      // 绑定到一个动态分配的端口上
      if (0 != ::getaddrinfo(NULL, "0", &hints, &res))
         throw CUDTException(1, 3, NET_ERROR);

      if (0 != ::bind(m_iSocket, res->ai_addr, res->ai_addrlen))
         throw CUDTException(1, 3, NET_ERROR);

      ::freeaddrinfo(res);
   }

   // 设置套接字参数：发送/接收缓冲区大小 及 接收超时时间
   setUDPSockOpt();
}

// 复用一个已建立的UDP连接
void CChannel::open(UDPSOCKET udpsock)
{
   m_iSocket = udpsock;
   setUDPSockOpt();
}

void CChannel::setUDPSockOpt()
{
   // 设置发送/接收缓冲区大小
   #if defined(BSD) || defined(OSX)
      // BSD system will fail setsockopt if the requested buffer size exceeds system maximum value
      int maxsize = 64000;
      if (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (char*)&m_iRcvBufSize, sizeof(int)))
         ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (char*)&maxsize, sizeof(int));
      if (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (char*)&m_iSndBufSize, sizeof(int)))
         ::setsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (char*)&maxsize, sizeof(int));
   #else
      // for other systems, if requested is greated than maximum, the maximum value will be automactally used
      if ((0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (char*)&m_iRcvBufSize, sizeof(int))) ||
          (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (char*)&m_iSndBufSize, sizeof(int))))
         throw CUDTException(1, 3, NET_ERROR);
   #endif

   timeval tv;
   tv.tv_sec = 0;
   #if defined (BSD) || defined (OSX)
      // Known BSD bug as the day I wrote this code.
      // A small time out value will cause the socket to block forever.
      tv.tv_usec = 10000;
   #else    // Ubuntu中执行下面的逻辑
      tv.tv_usec = 100;
   #endif

   // 设置接收超时时间
   #ifdef UNIX
      // Set non-blocking I/O
      // UNIX does not support SO_RCVTIMEO
      // 设置为非阻塞模式
      int opts = ::fcntl(m_iSocket, F_GETFL);
      if (-1 == ::fcntl(m_iSocket, F_SETFL, opts | O_NONBLOCK))
         throw CUDTException(1, 3, NET_ERROR);
   #elif WIN32
      DWORD ot = 1; //milliseconds
      if (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&ot, sizeof(DWORD)))
         throw CUDTException(1, 3, NET_ERROR);
   #else // Ubuntu中执行下面的逻辑，设置接收超时时间为100us
      // Set receiving time-out value
      if (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(timeval)))
         throw CUDTException(1, 3, NET_ERROR);
   #endif
}

// 直接调用系统API close()，关闭UDP套接字
void CChannel::close() const
{
   #ifndef WIN32
      ::close(m_iSocket);
   #else
      ::closesocket(m_iSocket);
   #endif
}

// 调用系统API getsockopt，获取内核发送缓冲区大小
int CChannel::getSndBufSize()
{
   socklen_t size = sizeof(socklen_t);
   ::getsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (char *)&m_iSndBufSize, &size);
   return m_iSndBufSize;
}

// 调用系统API getsockopt，获取内核接收缓冲区大小
int CChannel::getRcvBufSize()
{
   socklen_t size = sizeof(socklen_t);
   ::getsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (char *)&m_iRcvBufSize, &size);
   return m_iRcvBufSize;
}

// 调用系统API setsockopt，设置内核发送缓冲区大小
void CChannel::setSndBufSize(int size)
{
   m_iSndBufSize = size;
}

// 调用系统API setsockopt，设置内核接收缓冲区大小
void CChannel::setRcvBufSize(int size)
{
   m_iRcvBufSize = size;
}

// 调用系统API getsockname，获取本地地址
void CChannel::getSockAddr(sockaddr* addr) const
{
   socklen_t namelen = m_iSockAddrSize;
   ::getsockname(m_iSocket, addr, &namelen);
}

// 调用系统API getsockname，获取对端地址
void CChannel::getPeerAddr(sockaddr* addr) const
{
   socklen_t namelen = m_iSockAddrSize;
   ::getpeername(m_iSocket, addr, &namelen);
}

int CChannel::sendto(const sockaddr* addr, CPacket& packet) const
{
   // convert control information into network order
   // packet.getFlag() ！=0,说明是一个控制报文
   // 将控制报文转换成网络字节序
   // 判断是否是一个控制报文，如果是一个控制报文，将其转换成网络字节序
   if (packet.getFlag()){
      for (int i = 0, n = packet.getLength() / 4; i < n; ++ i){
         *((uint32_t *)packet.m_pcData + i) = htonl(*((uint32_t *)packet.m_pcData + i));
      }
   }
   // convert packet header into network order
   //for (int j = 0; j < 4; ++ j)
   //   packet.m_nHeader[j] = htonl(packet.m_nHeader[j]);
   // 将所有报文的包头转换成网络字节序
   uint32_t* p = packet.m_nHeader;
   for (int j = 0; j < 4; ++ j)
   {
      *p = htonl(*p);
      ++ p;
   }

   #ifndef WIN32
      msghdr mh;
      mh.msg_name = (sockaddr*)addr;
      mh.msg_namelen = m_iSockAddrSize;
      mh.msg_iov = (iovec*)packet.m_PacketVector;
      mh.msg_iovlen = 2;
      mh.msg_control = NULL;
      mh.msg_controllen = 0;
      mh.msg_flags = 0;
   
      // 调用系统API sendmsg, 发送一个packet
      int res = ::sendmsg(m_iSocket, &mh, 0);
   #else
      DWORD size = CPacket::m_iPktHdrSize + packet.getLength();
      int addrsize = m_iSockAddrSize;
      int res = ::WSASendTo(m_iSocket, (LPWSABUF)packet.m_PacketVector, 2, &size, 0, addr, addrsize, NULL, NULL);
      res = (0 == res) ? size : -1;
   #endif

   // convert back into local host order
   //for (int k = 0; k < 4; ++ k)
   //   packet.m_nHeader[k] = ntohl(packet.m_nHeader[k]);
   // 由于传入的参数packet是引用，所以这里需要重新转换成本地字节序，避免破环引用的数据
   p = packet.m_nHeader;
   for (int k = 0; k < 4; ++ k)
   {
      *p = ntohl(*p);
       ++ p;
   }

   // 重新将包头的flag信息转换成本地字节序
   if (packet.getFlag())
   {
      for (int l = 0, n = packet.getLength() / 4; l < n; ++ l)
         *((uint32_t *)packet.m_pcData + l) = ntohl(*((uint32_t *)packet.m_pcData + l));
   }

   return res;
}

int CChannel::recvfrom(sockaddr* addr, CPacket& packet) const
{
   #ifndef WIN32
      msghdr mh;   
      mh.msg_name = addr;
      mh.msg_namelen = m_iSockAddrSize;
      mh.msg_iov = packet.m_PacketVector;
      mh.msg_iovlen = 2;
      mh.msg_control = NULL;
      mh.msg_controllen = 0;
      mh.msg_flags = 0;

      #ifdef UNIX
         fd_set set;
         timeval tv;
         FD_ZERO(&set);
         FD_SET(m_iSocket, &set);
         tv.tv_sec = 0;
         tv.tv_usec = 10000;
         // 只是用来设置超时时间，等待套接字可读
         ::select(m_iSocket+1, &set, NULL, &set, &tv);
      #endif
      
      // 调用系统API recvmsg, 接收数据
      int res = ::recvmsg(m_iSocket, &mh, 0);
   #else
      DWORD size = CPacket::m_iPktHdrSize + packet.getLength();
      DWORD flag = 0;
      int addrsize = m_iSockAddrSize;

      int res = ::WSARecvFrom(m_iSocket, (LPWSABUF)packet.m_PacketVector, 2, &size, &flag, addr, &addrsize, NULL, NULL);
      res = (0 == res) ? size : -1;
   #endif

   if (res <= 0)
   {
      packet.setLength(-1);
      return -1;
   }

   packet.setLength(res - CPacket::m_iPktHdrSize);

   // convert back into local host order
   //for (int i = 0; i < 4; ++ i)
   //   packet.m_nHeader[i] = ntohl(packet.m_nHeader[i]);
   // 将包头转换成本地字节序
   uint32_t* p = packet.m_nHeader;
   for (int i = 0; i < 4; ++ i)
   {
      *p = ntohl(*p);
      ++ p;
   }

   // 将包头中的flag转换成本地字节序
   if (packet.getFlag())
   {
      for (int j = 0, n = packet.getLength() / 4; j < n; ++ j)
         *((uint32_t *)packet.m_pcData + j) = ntohl(*((uint32_t *)packet.m_pcData + j));
   }

   return packet.getLength();
}
