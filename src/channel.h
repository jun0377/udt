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
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 01/27/2011
*****************************************************************************/

#ifndef __UDT_CHANNEL_H__
#define __UDT_CHANNEL_H__


#include "udt.h"
#include "packet.h"

// 调用系统API socket/bind/sendmsg/recvmsg来实现UDP通信
class CChannel
{
public:
   CChannel();
   CChannel(int version);
   ~CChannel();

      // Functionality:
      //    Open a UDP channel.
      // Parameters:
      //    0) [in] addr: The local address that UDP will use.
      // Returned value:
      //    None.

   // 调用系统API socket/bind，新建一个UDP连接，必须进行地址绑定
   void open(const sockaddr* addr = NULL);

      // Functionality:
      //    Open a UDP channel based on an existing UDP socket.
      // Parameters:
      //    0) [in] udpsock: UDP socket descriptor.
      // Returned value:
      //    None.

   // 复用一个已建立的UDP连接
   void open(UDPSOCKET udpsock);

      // Functionality:
      //    Disconnect and close the UDP entity.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // 直接调用系统API，关闭UDP套接字
   void close() const;

      // Functionality:
      //    Get the UDP sending buffer size.
      // Parameters:
      //    None.
      // Returned value:
      //    Current UDP sending buffer size.

   // 调用系统API getsockopt，获取内核发送缓冲区大小
   int getSndBufSize();

      // Functionality:
      //    Get the UDP receiving buffer size.
      // Parameters:
      //    None.
      // Returned value:
      //    Current UDP receiving buffer size.

   //  调用系统API getsockopt，获取内核接收缓冲区大小
   int getRcvBufSize();

      // Functionality:
      //    Set the UDP sending buffer size.
      // Parameters:
      //    0) [in] size: expected UDP sending buffer size.
      // Returned value:
      //    None.

   // 调用系统API setsockopt，设置内核发送缓冲区大小
   void setSndBufSize(int size);

      // Functionality:
      //    Set the UDP receiving buffer size.
      // Parameters:
      //    0) [in] size: expected UDP receiving buffer size.
      // Returned value:
      //    None.

   // 调用系统API setsockopt，设置内核接收缓冲区大小
   void setRcvBufSize(int size);

      // Functionality:
      //    Query the socket address that the channel is using.
      // Parameters:
      //    0) [out] addr: pointer to store the returned socket address.
      // Returned value:
      //    None.

   // 调用系统API getsockname, 获取当前sockfd的本地地址信息
   void getSockAddr(sockaddr* addr) const;

      // Functionality:
      //    Query the peer side socket address that the channel is connect to.
      // Parameters:
      //    0) [out] addr: pointer to store the returned socket address.
      // Returned value:
      //    None.

   // 调用系统API getpeername,获取对端地址
   void getPeerAddr(sockaddr* addr) const;
      // Functionality:
      //    Send a packet to the given address.
      // Parameters:
      //    0) [in] addr: pointer to the destination address.
      //    1) [in] packet: reference to a CPacket entity.
      // Returned value:
      //    Actual size of data sent.

   // 调用系统API sendmsg, 发送一个packet
   int sendto(const sockaddr* addr, CPacket& packet) const;

      // Functionality:
      //    Receive a packet from the channel and record the source address.
      // Parameters:
      //    0) [in] addr: pointer to the source address.
      //    1) [in] packet: reference to a CPacket entity.
      // Returned value:
      //    Actual size of data received.

   // 调用系统API recvmsg, 接收数据，超时时间10ms
   int recvfrom(sockaddr* addr, CPacket& packet) const;

private:
   // 设置套接字属性：发送/接收缓冲区大小 及 接收超时时间
   void setUDPSockOpt();

private:
   // IPv4 or IPv6
   int m_iIPversion;                    // IP version
   // 地址长度，IPv6/IPv6的地址长度不同
   int m_iSockAddrSize;                 // socket address structure size (pre-defined to avoid run-time test)

   // UDP系统套接字
   UDPSOCKET m_iSocket;                 // socket descriptor

   // 内核发送缓冲区大小，默认为65536
   int m_iSndBufSize;                   // UDP sending buffer size
   // 内核接收缓冲区大小，默认为65536
   int m_iRcvBufSize;                   // UDP receiving buffer size
};


#endif
