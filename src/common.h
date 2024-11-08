/*****************************************************************************
Copyright (c) 2001 - 2009, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu, last updated 08/01/2009
*****************************************************************************/

#ifndef __UDT_COMMON_H__
#define __UDT_COMMON_H__


#ifndef WIN32
   #include <sys/time.h>
   #include <sys/uio.h>
   #include <pthread.h>
#else
   #include <windows.h>
#endif
#include <cstdlib>
#include "udt.h"


#ifdef WIN32
   // Windows compability
   typedef HANDLE pthread_t;
   typedef HANDLE pthread_mutex_t;
   typedef HANDLE pthread_cond_t;
   typedef DWORD pthread_key_t;
#endif


////////////////////////////////////////////////////////////////////////////////

// 实现了一种可以被中断的定时器
class CTimer
{
public:
   CTimer();
   ~CTimer();

public:

      // Functionality:
      //    Sleep for "interval" CCs.
      // Parameters:
      //    0) [in] interval: CCs to sleep.
      // Returned value:
      //    None.

   // 休眠一段时间
   void sleep(uint64_t interval);

      // Functionality:
      //    Seelp until CC "nexttime".
      // Parameters:
      //    0) [in] nexttime: next time the caller is waken up.
      // Returned value:
      //    None.

   // 休眠到下一个时间点
   void sleepto(uint64_t nexttime);

      // Functionality:
      //    Stop the sleep() or sleepto() methods.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // 更新下一次调度时间为当前时间，听过调用tick()立即退出休眠
   void interrupt();

      // Functionality:
      //    trigger the clock for a tick, for better granuality in no_busy_waiting timer.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // 解除sleep()和sleepto()中可能存在的阻塞
   void tick();

public:

      // Functionality:
      //    Read the CPU clock cycle into x.
      // Parameters:
      //    0) [out] x: to record cpu clock cycles.
      // Returned value:
      //    None.
      
   // 可以简单认为是获取us级时间戳
   static void rdtsc(uint64_t &x);

      // Functionality:
      //    return the CPU frequency.
      // Parameters:
      //    None.
      // Returned value:
      //    CPU frequency.

   // 获取CPU频率
   static uint64_t getCPUFrequency();

      // Functionality:
      //    check the current time, 64bit, in microseconds.(明明获取的是us，这个注释应该有误)
      // Parameters:
      //    None.
      // Returned value:
      //    current time in microseconds.

   // 获取当前时间的us级时间戳
   static uint64_t getTime();

      // Functionality:
      //    trigger an event such as new connection, close, new data, etc. for "select" call.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // 唤醒条件变量，触发一个事件
   static void triggerEvent();

      // Functionality:
      //    wait for an event to br triggered by "triggerEvent".
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // 阻塞等待，最多等待10ms
   static void waitForEvent();

      // Functionality:
      //    sleep for a short interval. exact sleep time does not matter
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // 简单休眠10us 
   static void sleep();

private:
   // 时间戳,us   Q;怎么没有实现这个函数呢？
   uint64_t getTimeInMicroSec();

private:
   // 下一次调度时间，ull = unsigned long long
   uint64_t m_ullSchedTime;             // next schedulled time

   // tick的条件变量和锁
   pthread_cond_t m_TickCond;
   pthread_mutex_t m_TickLock;

   // 事件的条件变量和锁
   static pthread_cond_t m_EventCond;
   static pthread_mutex_t m_EventLock;

private:
   // CPU时钟频率, 每ms多少个时钟周期
   static uint64_t s_ullCPUFrequency;	// CPU frequency : clock cycles per microsecond
   // 获取CPU时钟频率
   static uint64_t readCPUFrequency();
   // 是否使用微秒级时间戳,实际上可以获取到比微秒级更精准的时间戳
   static bool m_bUseMicroSecond;       // No higher resolution timer available, use gettimeofday().
};

////////////////////////////////////////////////////////////////////////////////

// 用于线程同步, CS = Critical Section，临界区
// 实现C++11标准中std::lock_guard的功能
class CGuard
{
public:
   CGuard(pthread_mutex_t& lock);
   ~CGuard();

public:
   // 进入临界区自动加锁，离开临界区自动解锁
   static void enterCS(pthread_mutex_t& lock);
   static void leaveCS(pthread_mutex_t& lock);

   // 初始化/释放　互斥锁
   static void createMutex(pthread_mutex_t& lock);
   static void releaseMutex(pthread_mutex_t& lock);

   // 初始化/释放 条件变量
   static void createCond(pthread_cond_t& cond);
   static void releaseCond(pthread_cond_t& cond);

private:
   pthread_mutex_t& m_Mutex;            // Alias name of the mutex to be protected
   int m_iLocked;                       // Locking status

   // 仅声明未实现，用来禁止拷贝复制和赋值操作
   CGuard& operator=(const CGuard&);
};



////////////////////////////////////////////////////////////////////////////////

// UDT Sequence Number 0 - (2^31 - 1)

// seqcmp: compare two seq#, considering the wraping
// seqlen: length from the 1st to the 2nd seq#, including both
// seqoff: offset from the 2nd to the 1st seq#
// incseq: increase the seq# by 1
// decseq: decrease the seq# by 1
// incseq: increase the seq# by a given offset

// UDT包的序列号，实现序列号的增加/减少/比较/偏移量计算等功能
class CSeqNo
{
public:
   // 比较两个序列号的大小
   inline static int seqcmp(int32_t seq1, int32_t seq2)
   {return (abs(seq1 - seq2) < m_iSeqNoTH) ? (seq1 - seq2) : (seq2 - seq1);}

   // 从第一个到第二个序列号的长度
   inline static int seqlen(int32_t seq1, int32_t seq2)
   {return (seq1 <= seq2) ? (seq2 - seq1 + 1) : (seq2 - seq1 + m_iMaxSeqNo + 2);}

   // 从第二个序列到第一个序列的偏移量
   inline static int seqoff(int32_t seq1, int32_t seq2)
   {
      if (abs(seq1 - seq2) < m_iSeqNoTH)
         return seq2 - seq1;

      if (seq1 < seq2)
         return seq2 - seq1 - m_iMaxSeqNo - 1;

      return seq2 - seq1 + m_iMaxSeqNo + 1;
   }

   // 序列号加一，当序列号大于m_iMaxSeqNo，则回卷到 0
   inline static int32_t incseq(int32_t seq)
   {return (seq == m_iMaxSeqNo) ? 0 : seq + 1;}

   // 序列号减一，当序列号小于0，则回卷到 m_iMaxSeqNo
   inline static int32_t decseq(int32_t seq)
   {return (seq == 0) ? m_iMaxSeqNo : seq - 1;}

   // 序列号加一个偏移量，当序列号加上偏移量大于m_iMaxSeqNo，则回卷到 0
   inline static int32_t incseq(int32_t seq, int32_t inc)
   {return (m_iMaxSeqNo - seq >= inc) ? seq + inc : seq - m_iMaxSeqNo + inc - 1;}

public:
   // 比较序列号时使用的阈值，是序列号最大值的一半；当两个序列号的差值小于这个阈值时，表示这两个序列号在逻辑上是相近的
   static const int32_t m_iSeqNoTH;             // threshold for comparing seq. no.
   // 序列号最大值
   static const int32_t m_iMaxSeqNo;            // maximum sequence number used in UDT
};

////////////////////////////////////////////////////////////////////////////////

// UDT ACK Sub-sequence Number: 0 - (2^31 - 1)

class CAckNo
{
public:
   inline static int32_t incack(int32_t ackno)
   {return (ackno == m_iMaxAckSeqNo) ? 0 : ackno + 1;}

public:
   static const int32_t m_iMaxAckSeqNo;         // maximum ACK sub-sequence number used in UDT
};

////////////////////////////////////////////////////////////////////////////////

// UDT Message Number: 0 - (2^29 - 1)
// UDT消息号，实现UDT消息号的比较/长度计算/偏移量计算等功能
class CMsgNo
{
public:
   inline static int msgcmp(int32_t msgno1, int32_t msgno2)
   {return (abs(msgno1 - msgno2) < m_iMsgNoTH) ? (msgno1 - msgno2) : (msgno2 - msgno1);}

   inline static int msglen(int32_t msgno1, int32_t msgno2)
   {return (msgno1 <= msgno2) ? (msgno2 - msgno1 + 1) : (msgno2 - msgno1 + m_iMaxMsgNo + 2);}

   inline static int msgoff(int32_t msgno1, int32_t msgno2)
   {
      if (abs(msgno1 - msgno2) < m_iMsgNoTH)
         return msgno2 - msgno1;

      if (msgno1 < msgno2)
         return msgno2 - msgno1 - m_iMaxMsgNo - 1;

      return msgno2 - msgno1 + m_iMaxMsgNo + 1;
   }

   inline static int32_t incmsg(int32_t msgno)
   {return (msgno == m_iMaxMsgNo) ? 0 : msgno + 1;}

public:
   static const int32_t m_iMsgNoTH;             // threshold for comparing msg. no.
   static const int32_t m_iMaxMsgNo;            // maximum message number used in UDT
};

////////////////////////////////////////////////////////////////////////////////

// 实现IP地址的比较/二进制网络地址和字符串转换的功能功能
struct CIPAddress
{
   static bool ipcmp(const sockaddr* addr1, const sockaddr* addr2, int ver = AF_INET);
   static void ntop(const sockaddr* addr, uint32_t ip[4], int ver = AF_INET);
   static void pton(sockaddr* addr, const uint32_t ip[4], int ver = AF_INET);
};

////////////////////////////////////////////////////////////////////////////////

// MD5计算
struct CMD5
{
   static void compute(const char* input, unsigned char result[16]);
};


#endif
