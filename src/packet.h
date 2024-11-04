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
   Yunhong Gu, last updated 01/02/2011
*****************************************************************************/

#ifndef __UDT_PACKET_H__
#define __UDT_PACKET_H__


#include "udt.h"

#ifdef WIN32
   struct iovec
   {
      int iov_len;
      char* iov_base;
   };
#endif

class CChannel;

class CPacket
{
friend class CChannel;
friend class CSndQueue;
friend class CRcvQueue;

public:
   // 序列号，注意：是一个引用，真实的值在m_nHeader[0]中
   int32_t& m_iSeqNo;                   // alias: sequence number
   // UDT消息号,注意：是一个引用，真实的值在m_nHeader[1]中
   int32_t& m_iMsgNo;                   // alias: message number
   // 时间戳，注意：是一个引用，真实的值在m_nHeader[2]中
   int32_t& m_iTimeStamp;               // alias: timestamp
   // socket ID，注意：是一个引用，真实的值在m_nHeader[3]中
   int32_t& m_iID;			// alias: socket ID
   // 负载数据，即m_PacketVector[1]中的数据
   char*& m_pcData;                     // alias: data/control information

   // 包头大小
   static const int m_iPktHdrSize;	// packet header size

public:
   CPacket();
   ~CPacket();

      // Functionality:
      //    Get the payload or the control information field length.
      // Parameters:
      //    None.
      // Returned value:
      //    the payload or the control information field length.

   // 获取负载数据的大小，不包含包头长度
   int getLength() const;

      // Functionality:
      //    Set the payload or the control information field length.
      // Parameters:
      //    0) [in] len: the payload or the control information field length.
      // Returned value:
      //    None.

   // 设置负载数据的大小，不包含包头长度
   void setLength(int len);

      // Functionality:
      //    Pack a Control packet.
      // Parameters:
      //    0) [in] pkttype: packet type filed.
      //    1) [in] lparam: pointer to the first data structure, explained by the packet type.
      //    2) [in] rparam: pointer to the second data structure, explained by the packet type.
      //    3) [in] size: size of rparam, in number of bytes;
      // Returned value:
      //    None.
   
   // 控制包打包，根据不同类型的报文执行不同的操作
   void pack(int pkttype, void* lparam = NULL, void* rparam = NULL, int size = 0);

      // Functionality:
      //    Read the packet vector.
      // Parameters:
      //    None.
      // Returned value:
      //    Pointer to the packet vector.

   // 获取完整报文，包括包头和payload
   iovec* getPacketVector();

      // Functionality:
      //    Read the packet flag.
      // Parameters:
      //    None.
      // Returned value:
      //    packet flag (0 or 1).

   // 获取报文中的flag,区分是数据包还是控制包，m_nHeader[0]的bit[0]
   int getFlag() const;

      // Functionality:
      //    Read the packet type.
      // Parameters:
      //    None.
      // Returned value:
      //    packet type filed (000 ~ 111).

   // 获取报文类型，ACK/ACK-2/NAK/各类控制报文
   int getType() const;

      // Functionality:
      //    Read the extended packet type.
      // Parameters:
      //    None.
      // Returned value:
      //    extended packet type filed (0x000 ~ 0xFFF).

   // 获取报文扩展类型，m_nHeader[0]的低16位
   int getExtendedType() const;

      // Functionality:
      //    Read the ACK-2 seq. no.
      // Parameters:
      //    None.
      // Returned value:
      //    packet header field (bit 16~31).

   // 获取ACK-2报文的序列号，m_nHeader[1]
   int32_t getAckSeqNo() const;

      // Functionality:
      //    Read the message boundary flag bit.
      // Parameters:
      //    None.
      // Returned value:
      //    packet header field [1] (bit 0~1).

   // 获取消息边界标志位，m_nHeader[1]的bit[0:1]
   int getMsgBoundary() const;

      // Functionality:
      //    Read the message inorder delivery flag bit.
      // Parameters:
      //    None.
      // Returned value:
      //    packet header field [1] (bit 2).

   // 获取消息顺序标志位，m_nHeader[1]的bit[2]
   bool getMsgOrderFlag() const;

      // Functionality:
      //    Read the message sequence number.
      // Parameters:
      //    None.
      // Returned value:
      //    packet header field [1] (bit 3~31).

   // 获取序列号，m_nHeader[1]的bit[3:31]
   int32_t getMsgSeq() const;

      // Functionality:
      //    Clone this packet.
      // Parameters:
      //    None.
      // Returned value:
      //    Pointer to the new packet.

   // 复制报文，包含包头和负载
   CPacket* clone() const;

protected:
   // 128bit的自定义包头
   uint32_t m_nHeader[4];               // The 128-bit header field
   // m_PacketVector[0]存储包头信息；m_PacketVector[1]存储负载信息
   iovec m_PacketVector[2];             // The 2-demension vector of UDT packet [header, data]

   // 数据对齐，提高内存中数据访问效率
   int32_t __pad;

protected:
   // 仅声明未实现，表示不支持赋值操作
   CPacket& operator=(const CPacket&);
};

////////////////////////////////////////////////////////////////////////////////

// 握手报文，固定大小：48byte
class CHandShake
{
public:
   CHandShake();

   // 按固定格式封包
   int serialize(char* buf, int& size);
   // 解包
   int deserialize(const char* buf, int size);

public:
   // 握手报文固定大小48byte
   static const int m_iContentSize;	// Size of hand shake data

public:
   // UDT版本号，共有四个版本
   int32_t m_iVersion;          // UDT version
   // UDT套接字类型
   int32_t m_iType;             // UDT socket type
   // 起始序列号，是一个随机值
   int32_t m_iISN;              // random initial sequence number
   // 最大报文段
   int32_t m_iMSS;              // maximum segment size
   // 流控窗口大小
   int32_t m_iFlightFlagSize;   // flow control window size
   // 握手过程中的请求类型，1：普通连接请求，0：交会连接请求，-1/-2：响应
   int32_t m_iReqType;          // connection request type: 1: regular connection request, 0: rendezvous connection request, -1/-2: response
   // UDT套接字ID
   int32_t m_iID;		// socket ID
   // 缓存
   int32_t m_iCookie;		// cookie
   // 对端IP地址
   uint32_t m_piPeerIP[4];	// The IP address that the peer's UDP port is bound to
};


#endif
