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
   Yunhong Gu, last updated 02/21/2013
*****************************************************************************/


#include "core.h"
#include "ccc.h"
#include <cmath>
#include <cstring>

CCC::CCC():
m_iSYNInterval(CUDT::m_iSYNInterval),
m_dPktSndPeriod(1.0),
m_dCWndSize(16.0),
m_iBandwidth(),
m_dMaxCWndSize(),
m_iMSS(),
m_iSndCurrSeqNo(),
m_iRcvRate(),
m_iRTT(),
m_pcParam(NULL),
m_iPSize(0),
m_UDT(),
m_iACKPeriod(0),
m_iACKInterval(0),
m_bUserDefinedRTO(false),
m_iRTO(-1),
m_PerfInfo()
{
}

CCC::~CCC()
{
   delete [] m_pcParam;
}

// 设置ACK发送的时间间隔,确保其不超过SYN包的间隔
void CCC::setACKTimer(int msINT)
{
   // 发送间隔最大不能超过10000ms
   m_iACKPeriod = msINT > m_iSYNInterval ? m_iSYNInterval : msINT;
}

// 设置基于数据包的ACK发送间隔，隔多少个包发送一次ACK
void CCC::setACKInterval(int pktINT)
{
   m_iACKInterval = pktINT;
}

// 重传超时时间
void CCC::setRTO(int usRTO)
{
   m_bUserDefinedRTO = true;
   m_iRTO = usRTO;
}

// 发送用户自定义控制包
void CCC::sendCustomMsg(CPacket& pkt) const
{
   CUDT* u = CUDT::getUDTHandle(m_UDT);

   if (NULL != u)
   {
      pkt.m_iID = u->m_PeerID;
      u->m_pSndQueue->sendto(u->m_pPeerAddr, pkt);
   }
}

// 性能探测
const CPerfMon* CCC::getPerfInfo()
{
   try
   {
      CUDT* u = CUDT::getUDTHandle(m_UDT);
      if (NULL != u)
         u->sample(&m_PerfInfo, false);
   }
   catch (...)
   {
      return NULL;
   }

   return &m_PerfInfo;
}

void CCC::setMSS(int mss)
{
   m_iMSS = mss;
}

void CCC::setBandwidth(int bw)
{
   m_iBandwidth = bw;
}

void CCC::setSndCurrSeqNo(int32_t seqno)
{
   m_iSndCurrSeqNo = seqno;
}

void CCC::setRcvRate(int rcvrate)
{
   m_iRcvRate = rcvrate;
}

void CCC::setMaxCWndSize(int cwnd)
{
   m_dMaxCWndSize = cwnd;
}

void CCC::setRTT(int rtt)
{
   m_iRTT = rtt;
}

void CCC::setUserParam(const char* param, int size)
{
   delete [] m_pcParam;
   m_pcParam = new char[size];
   memcpy(m_pcParam, param, size);
   m_iPSize = size;
}

//
CUDTCC::CUDTCC():
m_iRCInterval(),
m_LastRCTime(),
m_bSlowStart(),
m_iLastAck(),
m_bLoss(),
m_iLastDecSeq(),
m_dLastDecPeriod(),
m_iNAKCount(),
m_iDecRandom(),
m_iAvgNAKNum(),
m_iDecCount()
{
}

void CUDTCC::init()
{
   m_iRCInterval = m_iSYNInterval;
   m_LastRCTime = CTimer::getTime();
   setACKTimer(m_iRCInterval);

   m_bSlowStart = true;
   m_iLastAck = m_iSndCurrSeqNo;
   m_bLoss = false;
   m_iLastDecSeq = CSeqNo::decseq(m_iLastAck);
   m_dLastDecPeriod = 1;
   m_iAvgNAKNum = 0;
   m_iNAKCount = 0;
   m_iDecRandom = 1;

   m_dCWndSize = 16;
   m_dPktSndPeriod = 1;
}
/*
   1. 通过ACK包来调整发送速率
   2. 慢启动阶段：每当收到一个ACK时，会计算和上一个ACK的序列号差值，拥塞窗口都会增加这个差值
   3. 当拥塞窗口超过慢启动门限时，退出慢启动阶段，进入拥塞避免阶段
   4. 两种拥塞避免算法：
      4.1 接收端的接收速率不为0时：根据接收方的接收速率，来调整发送间隔
      4.2 接收端的接收速率为0时：根据当前的RTT和拥塞窗口大小计算发送速率，确保发送的包不会过于频繁
   5. 处于慢启动阶段或发送丢包时，不必调整发送间隔
   6. 如果需要调整发送速率，则根据带宽增量，计算新的发送间隔
*/
void CUDTCC::onACK(int32_t ack)
{
   // 用于计算可用带宽
   int64_t B = 0;
   // 用于计算带宽增量
   double inc = 0;
   // Note: 1/24/2012
   // The minimum increase parameter is increased from "1.0 / m_iMSS" to 0.01
   // because the original was too small and caused sending rate to stay at low level
   // for long time.

   // 最小增加参数，防止发送速率过低
   const double min_inc = 0.01;

   // 尚未达到需要进行速率控制的时间间隔，直接返回
   uint64_t currtime = CTimer::getTime();
   if (currtime - m_LastRCTime < (uint64_t)m_iRCInterval)
      return;

   // 更新速率控制的时间
   m_LastRCTime = currtime;

   // 慢启动阶段，增加拥塞窗口大小
   if (m_bSlowStart)
   {
      // 拥塞窗口增加
      m_dCWndSize += CSeqNo::seqlen(m_iLastAck, ack);
      // 更新最后的ACK序列号
      m_iLastAck = ack;

      // 如果拥塞窗口大小超过慢启动门限，则进入拥塞避免阶段
      if (m_dCWndSize > m_dMaxCWndSize)
      {
         // 已越过慢启动阶段，开始进入拥塞避免阶段
         m_bSlowStart = false;
         // 如果接收速率大于0，则根据接收方接收速率来限制发送间隔
         if (m_iRcvRate > 0){
            // 1000000.0 us == 1s，根据接收方接收速率来限制发送间隔
            m_dPktSndPeriod = 1000000.0 / m_iRcvRate;
         }
         else{
            // 如果接收速率为0，则根据当前的RTT和拥塞窗口大小计算发送速率
            m_dPktSndPeriod = (m_iRTT + m_iRCInterval) / m_dCWndSize;
         }
      }
   }
   else{
      // 更新拥塞窗口大小
      m_dCWndSize = m_iRcvRate / 1000000.0 * (m_iRTT + m_iRCInterval) + 16;
   }
   // During Slow Start, no rate increase
   // 慢启动阶段，不增加速率
   if (m_bSlowStart)
      return;

   // 如果发生丢包，则不增加速率
   if (m_bLoss)
   {
      m_bLoss = false;
      return;
   }

   // 计算可用带宽，即：当前带宽 - 发送速率
   B = (int64_t)(m_iBandwidth - 1000000.0 / m_dPktSndPeriod);
   // 限制可用带宽的最大值: 可用带宽不得超过当前带宽的1/9，防止发送过快导致网络拥塞
   if ((m_dPktSndPeriod > m_dLastDecPeriod) && ((m_iBandwidth / 9) < B))
      B = m_iBandwidth / 9;

   // 限制可用带宽的最小值，确保在极端情况下发送速率不会降低过低
   if (B <= 0){
      inc = min_inc;
   }
   else
   {
      // inc = max(10 ^ ceil(log10( B * MSS * 8 ) * Beta / MSS, 1/MSS)
      // Beta = 1.5 * 10^(-6)

      inc = pow(10.0, ceil(log10(B * m_iMSS * 8.0))) * 0.0000015 / m_iMSS;

      if (inc < min_inc)
         inc = min_inc;
   }

   // 根据带宽增量，计算新的发送间隔
   m_dPktSndPeriod = (m_dPktSndPeriod * m_iRCInterval) / (m_dPktSndPeriod * inc + m_iRCInterval);
}

void CUDTCC::onLoss(const int32_t* losslist, int)
{
   //Slow Start stopped, if it hasn't yet
   // 慢启动阶段
   if (m_bSlowStart)
   {
      m_bSlowStart = false;
      // 如果接收速率大于0，则根据接收速率来调整发送间隔
      if (m_iRcvRate > 0)
      {
         // Set the sending rate to the receiving rate.
         m_dPktSndPeriod = 1000000.0 / m_iRcvRate;
         return;
      }
      // If no receiving rate is observed, we have to compute the sending
      // rate according to the current window size, and decrease it
      // using the method below.
      // 如果接收速率为0，则根据当前的拥塞窗口大小和RTT来计算发送间隔
      m_dPktSndPeriod = m_dCWndSize / (m_iRTT + m_iRCInterval);
   }

   // 发生丢包
   m_bLoss = true;

   // 当前丢失的序列号大于上次速率降低调整时的序列号，表示网络状况可能变得更糟糕
   if (CSeqNo::seqcmp(losslist[0] & 0x7FFFFFFF, m_iLastDecSeq) > 0)
   {  
      // 记录上一次速率调整时的发送间隔
      m_dLastDecPeriod = m_dPktSndPeriod;
      // 增大发送间隔，增大原来的1.125倍
      m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.125);
      // 计算平均NAK数，使用滑动平均算法(指数加权移动平均)，权重因子=0.125；新平均=旧平均×(1−α)+新值×α
      m_iAvgNAKNum = (int)ceil(m_iAvgNAKNum * 0.875 + m_iNAKCount * 0.125);
      // 重置NAK计数器
      m_iNAKCount = 1;
      // 重置减少次数计数器
      m_iDecCount = 1;

      // 记录速率降低时的序列号
      m_iLastDecSeq = m_iSndCurrSeqNo;

      // remove global synchronization using randomization
      // 使用当前序列号作为随机种子
      srand(m_iLastDecSeq);
      // 随机化
      m_iDecRandom = (int)ceil(m_iAvgNAKNum * (double(rand()) / RAND_MAX));
      // 确保m_iDecRandom至少为1
      if (m_iDecRandom < 1)
         m_iDecRandom = 1;
   }
   // (m_iDecCount ++ < 5)表示一个拥塞周期内，最多允许减少5次发送速率，避免短时间内多次调整发送速率
   // (0 == (++ m_iNAKCount % m_iDecRandom))，随机化设计，避免所有连接在同一时刻尽心速率调整，导致网络的进一步拥塞
   else if ((m_iDecCount ++ < 5) && (0 == (++ m_iNAKCount % m_iDecRandom)))
   {
      // 0.875^5 = 0.51, rate should not be decreased by more than half within a congestion period
      // 增加发送周期，减小发送速率
      m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.125);
      // 更新上次减少序列号
      m_iLastDecSeq = m_iSndCurrSeqNo;
   }
}

void CUDTCC::onTimeout()
{
   // 慢启动阶段
   if (m_bSlowStart)
   {
      // 慢启动阶段发送超时，退出慢启动阶段
      m_bSlowStart = false;
      // 调整发送间隔
      if (m_iRcvRate > 0)
         m_dPktSndPeriod = 1000000.0 / m_iRcvRate;
      else
         m_dPktSndPeriod = m_dCWndSize / (m_iRTT + m_iRCInterval);
   }
   else
   {
      /*
      m_dLastDecPeriod = m_dPktSndPeriod;
      m_dPktSndPeriod = ceil(m_dPktSndPeriod * 2);
      m_iLastDecSeq = m_iLastAck;
      */
   }
}
