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

#include <cmath>
#include "common.h"
#include "window.h"
#include <algorithm>

using namespace std;

CACKWindow::CACKWindow(int size):
m_piACKSeqNo(NULL),
m_piACK(NULL),
m_pTimeStamp(NULL),
m_iSize(size),
m_iHead(0),
m_iTail(0)
{
   // 记录窗口中包的序列号
   m_piACKSeqNo = new int32_t[m_iSize];
   // 记录窗口中包含的确认序列号的包序列号
   m_piACK = new int32_t[m_iSize];
   // 每个ACK包的发送时间戳
   m_pTimeStamp = new uint64_t[m_iSize];

   m_piACKSeqNo[0] = -1;
}

CACKWindow::~CACKWindow()
{
   delete [] m_piACKSeqNo;
   delete [] m_piACK;
   delete [] m_pTimeStamp;
}

void CACKWindow::store(int32_t seq, int32_t ack)
{
   // 记录序列号
   m_piACKSeqNo[m_iHead] = seq;
   // 记录ACK
   m_piACK[m_iHead] = ack;
   // 记录时间戳
   m_pTimeStamp[m_iHead] = CTimer::getTime();

   // 更新头指针
   m_iHead = (m_iHead + 1) % m_iSize;

   // overwrite the oldest ACK since it is not likely to be acknowledged
   // 覆盖最早的ACK，因为它已经不太可能被确认
   if (m_iHead == m_iTail)
      m_iTail = (m_iTail + 1) % m_iSize;
}

int CACKWindow::acknowledge(int32_t seq, int32_t& ack)
{
   // 窗口中有数据
   if (m_iHead >= m_iTail)
   {
      // Head has not exceeded the physical boundary of the window
      // 头部未超出窗口的物理边界

      // 遍历窗口
      for (int i = m_iTail, n = m_iHead; i < n; ++ i)
      {
         // looking for indentical ACK Seq. No.
         // 找到匹配的序列号
         if (seq == m_piACKSeqNo[i])
         {
            // return the Data ACK it carried
            // 将找到的数据包序列号赋值给ack
            ack = m_piACK[i];

            // calculate RTT
            // 计算RTT
            int rtt = int(CTimer::getTime() - m_pTimeStamp[i]);

            // 更新尾指针，尾指针追上了头指针，说明窗口中没有数据
            if (i + 1 == m_iHead)
            {
               m_iTail = m_iHead = 0;
               m_piACKSeqNo[0] = -1;
            }
            else
               m_iTail = (i + 1) % m_iSize;

            return rtt;
         }
      }

      // Bad input, the ACK node has been overwritten
      // 如果没有找到匹配的序列号，返回 -1
      return -1;
   }

   // Head has exceeded the physical window boundary, so it is behind tail
   // 头指针超出了窗口边界，环形缓冲区
   for (int j = m_iTail, n = m_iHead + m_iSize; j < n; ++ j)
   {
      // looking for indentical ACK seq. no.
      // 查找匹配的序列号
      if (seq == m_piACKSeqNo[j % m_iSize])
      {
         // return Data ACK
         // 将找到的数据包序列号赋值给ack
         j %= m_iSize;
         ack = m_piACK[j];

         // calculate RTT
         // 计算RTT
         int rtt = int(CTimer::getTime() - m_pTimeStamp[j]);

         // 更新尾指针
         if (j == m_iHead)
         {
            m_iTail = m_iHead = 0;
            m_piACKSeqNo[0] = -1;
         }
         else
            m_iTail = (j + 1) % m_iSize;

         return rtt;
      }
   }

   // bad input, the ACK node has been overwritten
   return -1;
}

////////////////////////////////////////////////////////////////////////////////

CPktTimeWindow::CPktTimeWindow(int asize, int psize):
m_iAWSize(asize),
m_piPktWindow(NULL),
m_iPktWindowPtr(0),
m_iPWSize(psize),
m_piProbeWindow(NULL),
m_iProbeWindowPtr(0),
m_iLastSentTime(0),
m_iMinPktSndInt(1000000),
m_LastArrTime(),
m_CurrArrTime(),
m_ProbeTime()
{
   m_piPktWindow = new int[m_iAWSize];
   m_piPktReplica = new int[m_iAWSize];
   m_piProbeWindow = new int[m_iPWSize];
   m_piProbeReplica = new int[m_iPWSize];

   m_LastArrTime = CTimer::getTime();

   for (int i = 0; i < m_iAWSize; ++ i)
      m_piPktWindow[i] = 1000000;

   for (int k = 0; k < m_iPWSize; ++ k)
      m_piProbeWindow[k] = 1000;
}

CPktTimeWindow::~CPktTimeWindow()
{
   delete [] m_piPktWindow;
   delete [] m_piPktReplica;
   delete [] m_piProbeWindow;
   delete [] m_piProbeReplica;
}

int CPktTimeWindow::getMinPktSndInt() const
{
   return m_iMinPktSndInt;
}

int CPktTimeWindow::getPktRcvSpeed() const
{
   // get median value, but cannot change the original value order in the window
   // 从 m_piPktWindow 数组复制数据到 m_piPktReplica 数组，不改变原数组的值顺序
   std::copy(m_piPktWindow, m_piPktWindow + m_iAWSize - 1, m_piPktReplica);
   // 排序获取中值
   std::nth_element(m_piPktReplica, m_piPktReplica + (m_iAWSize / 2), m_piPktReplica + m_iAWSize - 1);
   int median = m_piPktReplica[m_iAWSize / 2];

   int count = 0;
   int sum = 0;
   // 上限值，中值乘以8
   int upper = median << 3;
   // 下限值，中值除以8
   int lower = median >> 3;

   // median filtering
   // 中值滤波，去除过大或过小的值，降低因网络抖动或突发延迟对极端结果的影响
   int* p = m_piPktWindow;
   for (int i = 0, n = m_iAWSize; i < n; ++ i)
   {
      // 当前元素在上限和下限之间（有效值）
      if ((*p < upper) && (*p > lower))
      {
         ++ count;
         sum += *p;
      }
      ++ p;
   }

   // claculate speed, or return 0 if not enough valid value
   // 计算接收速度，如果有效值计数器大于数组大小的一半（即有效值足够多）
   if (count > (m_iAWSize >> 1)){
      // 小数向上圆整成整数,每秒接收到多少个包
      return (int)ceil(1000000.0 / (sum / count));
   }
   else{
      return 0;
   }
}

int CPktTimeWindow::getBandwidth() const
{
   // get median value, but cannot change the original value order in the window
   // 排序获取中值
   std::copy(m_piProbeWindow, m_piProbeWindow + m_iPWSize - 1, m_piProbeReplica);
   std::nth_element(m_piProbeReplica, m_piProbeReplica + (m_iPWSize / 2), m_piProbeReplica + m_iPWSize - 1);
   int median = m_piProbeReplica[m_iPWSize / 2];

   int count = 1;
   int sum = median;
   // 有效值的上下限
   int upper = median << 3;
   int lower = median >> 3;

   // median filtering
   // 中值滤波
   int* p = m_piProbeWindow;
   for (int i = 0, n = m_iPWSize; i < n; ++ i)
   {
      if ((*p < upper) && (*p > lower))
      {
         ++ count;
         sum += *p;
      }
      ++ p;
   }

   // 每秒传输多少个数据包
   return (int)ceil(1000000.0 / (double(sum) / double(count)));
}

void CPktTimeWindow::onPktSent(int currtime)
{
   int interval = currtime - m_iLastSentTime;

   // 更新包发送的最小间隔
   if ((interval < m_iMinPktSndInt) && (interval > 0))
      m_iMinPktSndInt = interval;

   // 更新最后一个发送包的时间
   m_iLastSentTime = currtime;
}

void CPktTimeWindow::onPktArrival()
{
   m_CurrArrTime = CTimer::getTime();

   // record the packet interval between the current and the last one
   // 记录两个报文间的接收时间间隔,用于统计接收速度
   *(m_piPktWindow + m_iPktWindowPtr) = int(m_CurrArrTime - m_LastArrTime);

   // the window is logically circular
   // 环形缓冲区
   ++ m_iPktWindowPtr;
   if (m_iPktWindowPtr == m_iAWSize)
      m_iPktWindowPtr = 0;

   // remember last packet arrival time
   // 记录最后一个报文到达的时间
   m_LastArrTime = m_CurrArrTime;
}

void CPktTimeWindow::probe1Arrival()
{
   m_ProbeTime = CTimer::getTime();
}

void CPktTimeWindow::probe2Arrival()
{
   m_CurrArrTime = CTimer::getTime();

   // record the probing packets interval
   // 记录探测报文的包间隔
   *(m_piProbeWindow + m_iProbeWindowPtr) = int(m_CurrArrTime - m_ProbeTime);
   // the window is logically circular
   // 环形缓冲区
   ++ m_iProbeWindowPtr;
   if (m_iProbeWindowPtr == m_iPWSize)
      m_iProbeWindowPtr = 0;
}
