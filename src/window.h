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
   Yunhong Gu, last updated 01/22/2011
*****************************************************************************/

#ifndef __UDT_WINDOW_H__
#define __UDT_WINDOW_H__


#ifndef WIN32
   #include <sys/time.h>
   #include <time.h>
#endif
#include "udt.h"


// 记录和管理ACK数据，从而帮助计算往返时间RTT以及数据包的传输状态
class CACKWindow
{
public:
   CACKWindow(int size = 1024);
   ~CACKWindow();

      // Functionality:
      //    Write an ACK record into the window.
      // Parameters:
      //    0) [in] seq: ACK seq. no.
      //    1) [in] ack: DATA ACK no.
      // Returned value:
      //    None.

   // 向窗口中写入一条记录
   void store(int32_t seq, int32_t ack);

      // Functionality:
      //    Search the ACK-2 "seq" in the window, find out the DATA "ack" and caluclate RTT .
      // Parameters:
      //    0) [in] seq: ACK-2 seq. no.
      //    1) [out] ack: the DATA ACK no. that matches the ACK-2 no.
      // Returned value:
      //    RTT.

   // 在窗口中查找指定的 ACK-2 序列号，并计算往返时间 (RTT)
   int acknowledge(int32_t seq, int32_t& ack);

private:
   // 记录序列号的数组
   int32_t* m_piACKSeqNo;       // Seq. No. for the ACK packet
   // 记录ACK的数组
   int32_t* m_piACK;            // Data Seq. No. carried by the ACK packet
   // 记录发送时间戳，用来计算RTT
   uint64_t* m_pTimeStamp;      // The timestamp when the ACK was sent

   // 窗口大小
   int m_iSize;                 // Size of the ACK history window
   // 指向最新的包
   int m_iHead;                 // Pointer to the lastest ACK record
   // 指向最旧的包
   int m_iTail;                 // Pointer to the oldest ACK record

private:
   // 仅声明未实现，相当于禁用拷贝构造
   CACKWindow(const CACKWindow&);
   // 仅声明未实现，相当于禁用赋值运算
   CACKWindow& operator=(const CACKWindow&);
};

////////////////////////////////////////////////////////////////////////////////

// 用于记录和估计数据包发送和接收时间信息的类。它提供了计算最小发送间隔、接收速度和带宽的功能
class CPktTimeWindow
{
public:
   CPktTimeWindow(int asize = 16, int psize = 16);
   ~CPktTimeWindow();

      // Functionality:
      //    read the minimum packet sending interval.
      // Parameters:
      //    None.
      // Returned value:
      //    minimum packet sending interval (microseconds).

   // 最小的包发送间隔
   int getMinPktSndInt() const;

      // Functionality:
      //    Calculate the packes arrival speed.
      // Parameters:
      //    None.
      // Returned value:
      //    Packet arrival speed (packets per second).

   // 计算接收速率：每秒多少个包
   int getPktRcvSpeed() const;

      // Functionality:
      //    Estimate the bandwidth.
      // Parameters:
      //    None.
      // Returned value:
      //    Estimated bandwidth (packets per second).

   // 估算带宽,每秒多少个包
   int getBandwidth() const;

      // Functionality:
      //    Record time information of a packet sending.
      // Parameters:
      //    0) currtime: timestamp of the packet sending.
      // Returned value:
      //    None.

   // 发送时调用，记录报文发送的时间戳
   void onPktSent(int currtime);

      // Functionality:
      //    Record time information of an arrived packet.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // 接收时调用，记录报文接收时的时间戳
   void onPktArrival();

      // Functionality:
      //    Record the arrival time of the first probing packet.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // 记录第一个探测报文到达的时间戳，其实就是当前时间戳
   void probe1Arrival();

      // Functionality:
      //    Record the arrival time of the second probing packet and the interval between packet pairs.
      // Parameters:
      //    None.
      // Returned value:
      //    None.
   
   // 记录第二个探测报文到达的时间和报文对之间的间隔
   void probe2Arrival();

private:
   // 接收速率统计窗口
   int m_iAWSize;               // size of the packet arrival history window
   // 指向一个int数组，记录两个报文间的接收时间间隔，以微秒为单位，用于计算接收速度和估算带宽
   int* m_piPktWindow;          // packet information window
   // m_piPktWindow的副本，避免直接操作m_piPktWindow
   int* m_piPktReplica;
   // 指向当前存储位置
   int m_iPktWindowPtr;         // position pointer of the packet info. window.

   // 探测报文窗口
   int m_iPWSize;               // size of probe history window size
   // 记录探测报文的包间隔
   int* m_piProbeWindow;        // record inter-packet time for probing packet pairs
   // m_piProbeWindow的副本，避免直接操作m_piProbeWindow
   int* m_piProbeReplica;
   // 指向当前存储位置
   int m_iProbeWindowPtr;       // position pointer to the probing window

   // 最后一个数据包的发送时间
   int m_iLastSentTime;         // last packet sending time
   // 包发送的最小间隔
   int m_iMinPktSndInt;         // Minimum packet sending interval

   // 最后一个数据包的接收时间戳
   uint64_t m_LastArrTime;      // last packet arrival time
   // 报文接收时的时间戳
   uint64_t m_CurrArrTime;      // current packet arrival time
   uint64_t m_ProbeTime;        // arrival time of the first probing packet

private:
   CPktTimeWindow(const CPktTimeWindow&);
   CPktTimeWindow &operator=(const CPktTimeWindow&);
};


#endif
