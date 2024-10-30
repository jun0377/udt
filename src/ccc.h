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
   Yunhong Gu, last updated 02/28/2012
*****************************************************************************/


#ifndef __UDT_CCC_H__
#define __UDT_CCC_H__


#include "udt.h"
#include "packet.h"


// CCC == Congestion Control Class
class UDT_API CCC
{
friend class CUDT;

public:
   CCC();
   virtual ~CCC();

private:
   CCC(const CCC&);
   CCC& operator=(const CCC&) {return *this;}

public:

      // Functionality:
      //    Callback function to be called (only) at the start of a UDT connection.
      //    note that this is different from CCC(), which is always called.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // UDT连接初始化时自动调用
   virtual void init() {}

      // Functionality:
      //    Callback function to be called when a UDT connection is closed.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // UDT连接关闭时自动调用
   virtual void close() {}

      // Functionality:
      //    Callback function to be called when an ACK packet is received.
      // Parameters:
      //    0) [in] ackno: the data sequence number acknowledged by this ACK.
      // Returned value:
      //    None.

   // 收到一个ACK包时自动调用
   virtual void onACK(int32_t) {}

      // Functionality:
      //    Callback function to be called when a loss report is received.
      // Parameters:
      //    0) [in] losslist: list of sequence number of packets, in the format describled in packet.cpp.
      //    1) [in] size: length of the loss list.
      // Returned value:
      //    None.

   // 发生丢包时自动调用
   virtual void onLoss(const int32_t*, int) {}

      // Functionality:
      //    Callback function to be called when a timeout event occurs.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // 当超时事件发生时自动调用
   virtual void onTimeout() {}

      // Functionality:
      //    Callback function to be called when a data is sent.
      // Parameters:
      //    0) [in] seqno: the data sequence number.
      //    1) [in] size: the payload size.
      // Returned value:
      //    None.

   // 发送数据时自动调用
   virtual void onPktSent(const CPacket*) {}

      // Functionality:
      //    Callback function to be called when a data is received.
      // Parameters:
      //    0) [in] seqno: the data sequence number.
      //    1) [in] size: the payload size.
      // Returned value:
      //    None.

   // 接收到数据时自动调用
   virtual void onPktReceived(const CPacket*) {}

      // Functionality:
      //    Callback function to Process a user defined packet.
      // Parameters:
      //    0) [in] pkt: the user defined packet.
      // Returned value:
      //    None.

   // 传输用户自定义数据包时自动调用
   virtual void processCustomMsg(const CPacket*) {}

protected:

      // Functionality:
      //    Set periodical acknowldging and the ACK period.
      // Parameters:
      //    0) [in] msINT: the period to send an ACK.
      // Returned value:
      //    None.

   // 设置周期性ACK发送的时间间隔
   void setACKTimer(int msINT);

      // Functionality:
      //    Set packet-based acknowldging and the number of packets to send an ACK.
      // Parameters:
      //    0) [in] pktINT: the number of packets to send an ACK.
      // Returned value:
      //    None.

   // 设置基于数据包的ACK发送间隔
   void setACKInterval(int pktINT);

      // Functionality:
      //    Set RTO value.
      // Parameters:
      //    0) [in] msRTO: RTO in macroseconds.
      // Returned value:
      //    None.

   // 设置重传超时（RTO）值
   void setRTO(int usRTO);

      // Functionality:
      //    Send a user defined control packet.
      // Parameters:
      //    0) [in] pkt: user defined packet.
      // Returned value:
      //    None.

   // 发送用户自定义控制包
   void sendCustomMsg(CPacket& pkt) const;

      // Functionality:
      //    retrieve performance information.
      // Parameters:
      //    None.
      // Returned value:
      //    Pointer to a performance info structure.

   // 获取性能信息
   const CPerfMon* getPerfInfo();

      // Functionality:
      //    Set user defined parameters.
      // Parameters:
      //    0) [in] param: the paramters in one buffer.
      //    1) [in] size: the size of the buffer.
      // Returned value:
      //    None.

   // 设置用户自定义参数
   void setUserParam(const char* param, int size);

private:
   void setMSS(int mss);
   void setMaxCWndSize(int cwnd);
   void setBandwidth(int bw);
   void setSndCurrSeqNo(int32_t seqno);
   void setRcvRate(int rcvrate);
   void setRTT(int rtt);

protected:
   // SYN包间隔,默认为10000 ms
   const int32_t& m_iSYNInterval;	// UDT constant parameter, SYN

   // 数据包发送间隔，us,默认为1us
   double m_dPktSndPeriod;              // Packet sending period, in microseconds
   // 拥塞窗口，单位：packet; 默认为16
   double m_dCWndSize;                  // Congestion window size, in packets

   // 每秒传输的包数：上行还是下行？
   int m_iBandwidth;			// estimated bandwidth, packets per second
   // 拥塞窗口最大值,单位:packet，相当于TCP中的慢启动门限
   double m_dMaxCWndSize;               // maximum cwnd size, in packets

   // 最大包大小
   int m_iMSS;				// Maximum Packet Size, including all packet headers
   // 发送方当前最大的序列号
   int32_t m_iSndCurrSeqNo;		// current maximum seq no sent out
   // 接收方每秒收到的包数，用来衡量接收方的接收速率
   int m_iRcvRate;			// packet arrive rate at receiver side, packets per second
   // 估算的rtt，us
   int m_iRTT;				// current estimated RTT, microsecond

   // 用户自定义参数
   char* m_pcParam;			// user defined parameter
   // 用户自定义参数的大小
   int m_iPSize;			// size of m_pcParam

private:
   // 将拥塞控制算法和UDT实体关联起来
   UDTSOCKET m_UDT;                     // The UDT entity that this congestion control algorithm is bound to

   // 周期性发送ACK的间隔，ms
   int m_iACKPeriod;                    // Periodical timer to send an ACK, in milliseconds
   // 一个ACK确认的数据包数量
   int m_iACKInterval;                  // How many packets to send one ACK, in packets

   // 用户是否自定义重传超时
   bool m_bUserDefinedRTO;              // if the RTO value is defined by users
   // 重传超时，us
   int m_iRTO;                          // RTO value, microseconds

   // 状态信息
   CPerfMon m_PerfInfo;                 // protocol statistics information
};

// 工厂模式
class CCCVirtualFactory
{
public:
   virtual ~CCCVirtualFactory() {}

   virtual CCC* create() = 0;
   virtual CCCVirtualFactory* clone() = 0;
};

template <class T>
class CCCFactory: public CCCVirtualFactory
{
public:
   virtual ~CCCFactory() {}

   virtual CCC* create() {return new T;}
   virtual CCCVirtualFactory* clone() {return new CCCFactory<T>;}
};

// UDT拥塞控制类
class CUDTCC: public CCC
{
public:
   CUDTCC();

public:
   // 初始化，在UDT连接开始时调用
   virtual void init();
   // 收到ACK包时调用
   virtual void onACK(int32_t);
   // 发生丢包时调用
   virtual void onLoss(const int32_t*, int);
   // 超时事件发生时调用
   virtual void onTimeout();

private:
   // UDT 速率控制间隔，用于控制数据发送的频率
   int m_iRCInterval;			// UDT Rate control interval
   // 上次速率增加的时间，用于计算速率变化
   uint64_t m_LastRCTime;		// last rate increase time
   // 是否处于慢启动阶段
   bool m_bSlowStart;			// if in slow start phase
   // 最后确认的序列号，用于跟踪已确认的数据
   int32_t m_iLastAck;			// last ACKed seq no
   // 自上次速率增加以来是否发生丢包，用于拥塞控制
   bool m_bLoss;			// if loss happened since last rate increase
   // 上次速率减少时发送的最大数据包序列号，用于拥塞控制
   int32_t m_iLastDecSeq;		// max pkt seq no sent out when last decrease happened
   // 上次速率减少时的包发送周期值，用于调整发送速率
   double m_dLastDecPeriod;		// value of pktsndperiod when last decrease happened
   // NAK 计数器，用于跟踪丢包情况
   int m_iNAKCount;                     // NAK counter
   // 一个随机值，用于控制发送速率减少的随机化程度
   int m_iDecRandom;                    // random threshold on decrease by number of loss events
   // 每次拥塞的平均 NAK 数量，用于评估网络状况
   int m_iAvgNAKNum;                    // average number of NAKs per congestion
   // 用于统计在当前拥塞周期内减少了多少次发送速率，用于拥塞控制算法的决策
   int m_iDecCount;			// number of decreases in a congestion epoch
};

#endif
