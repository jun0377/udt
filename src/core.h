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
   Yunhong Gu, last updated 02/28/2012
*****************************************************************************/

#ifndef __UDT_CORE_H__
#define __UDT_CORE_H__


#include "udt.h"
#include "common.h"
#include "list.h"
#include "buffer.h"
#include "window.h"
#include "packet.h"
#include "channel.h"
#include "api.h"
#include "ccc.h"
#include "cache.h"
#include "queue.h"

enum UDTSockType {UDT_STREAM = 1, UDT_DGRAM};

class CUDT
{
friend class CUDTSocket;
friend class CUDTUnited;
friend class CCC;
friend struct CUDTComp;
friend class CCache<CInfoBlock>;
friend class CRendezvousQueue;
friend class CSndQueue;
friend class CRcvQueue;
friend class CSndUList;
friend class CRcvUList;

private: // constructor and desctructor
   CUDT();
   CUDT(const CUDT& ancestor);
   const CUDT& operator=(const CUDT&) {return *this;}
   ~CUDT();

public: //API
   static int startup();
   static int cleanup();
   // 创建一个新UDT socket
   static UDTSOCKET socket(int af, int type = SOCK_STREAM, int protocol = 0);
   // 将UDT实例和UDP套接字关联起来
   static int bind(UDTSOCKET u, const sockaddr* name, int namelen);
   static int bind(UDTSOCKET u, UDPSOCKET udpsock);
   // 设置为listener
   static int listen(UDTSOCKET u, int backlog);
   // 接受连接
   static UDTSOCKET accept(UDTSOCKET u, sockaddr* addr, int* addrlen);
   // 连接
   static int connect(UDTSOCKET u, const sockaddr* name, int namelen);
   // 关闭一个UDT socket
   static int close(UDTSOCKET u);
   // 获取对端地址
   static int getpeername(UDTSOCKET u, sockaddr* name, int* namelen);
   // 获取本地地址
   static int getsockname(UDTSOCKET u, sockaddr* name, int* namelen);
   // 获取套接字参数
   static int getsockopt(UDTSOCKET u, int level, UDTOpt optname, void* optval, int* optlen);
   // 设置套接字参数
   static int setsockopt(UDTSOCKET u, int level, UDTOpt optname, const void* optval, int optlen);
   // 将数据放入发送缓冲区中
   static int send(UDTSOCKET u, const char* buf, int len, int flags);
   // 从接收缓冲区中读取数据
   static int recv(UDTSOCKET u, char* buf, int len, int flags);
   // 将数据放入发送缓冲区中，相较于send,允许设置消息的生存时间和是否顺序发送
   static int sendmsg(UDTSOCKET u, const char* buf, int len, int ttl = -1, bool inorder = false);
   // 和recvmsg搭配使用
   static int recvmsg(UDTSOCKET u, char* buf, int len);
   // 发送文件，按块发送
   static int64_t sendfile(UDTSOCKET u, std::fstream& ifs, int64_t& offset, int64_t size, int block = 364000);
   // 接收文件
   static int64_t recvfile(UDTSOCKET u, std::fstream& ofs, int64_t& offset, int64_t size, int block = 7280000);
   // 多路复用
   static int select(int nfds, ud_set* readfds, ud_set* writefds, ud_set* exceptfds, const timeval* timeout);
   static int selectEx(const std::vector<UDTSOCKET>& fds, std::vector<UDTSOCKET>* readfds, std::vector<UDTSOCKET>* writefds, std::vector<UDTSOCKET>* exceptfds, int64_t msTimeOut);
   // epoll
   static int epoll_create();
   static int epoll_add_usock(const int eid, const UDTSOCKET u, const int* events = NULL);
   static int epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
   static int epoll_remove_usock(const int eid, const UDTSOCKET u);
   static int epoll_remove_ssock(const int eid, const SYSSOCKET s);
   static int epoll_wait(const int eid, std::set<UDTSOCKET>* readfds, std::set<UDTSOCKET>* writefds, int64_t msTimeOut, std::set<SYSSOCKET>* lrfds = NULL, std::set<SYSSOCKET>* wrfds = NULL);
   static int epoll_release(const int eid);
   // 错误处理
   static CUDTException& getlasterror();
   // 性能监控
   static int perfmon(UDTSOCKET u, CPerfMon* perf, bool clear = true);
   // 套接字状态
   static UDTSTATUS getsockstate(UDTSOCKET u);

public: // internal API
   // 根据UDT socket获取CUDT实例
   static CUDT* getUDTHandle(UDTSOCKET u);

private:
      // Functionality:
      //    initialize a UDT entity and bind to a local address.
      // Parameters:
      //    None.
      // Returned value:
      //    None.
   // 开启一个UDT连接，初始化socket相关属性
   void open();

      // Functionality:
      //    Start listening to any connection request.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // 监听UDT连接
   void listen();

      // Functionality:
      //    Connect to a UDT entity listening at address "peer".
      // Parameters:
      //    0) [in] peer: The address of the listening UDT entity.
      // Returned value:
      //    None.

   // 连接指定的地址
   void connect(const sockaddr* peer);

      // Functionality:
      //    Process the response handshake packet.
      // Parameters:
      //    0) [in] pkt: handshake packet.
      // Returned value:
      //    Return 0 if connected, positive value if connection is in progress, otherwise error code.

   // 处理收到的握手包
   int connect(const CPacket& pkt) throw ();

      // Functionality:
      //    Connect to a UDT entity listening at address "peer", which has sent "hs" request.
      // Parameters:
      //    0) [in] peer: The address of the listening UDT entity.
      //    1) [in/out] hs: The handshake information sent by the peer side (in), negotiated value (out).
      // Returned value:
      //    None.

   void connect(const sockaddr* peer, CHandShake* hs);

      // Functionality:
      //    Close the opened UDT entity.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   void close();

      // Functionality:
      //    Request UDT to send out a data block "data" with size of "len".
      // Parameters:
      //    0) [in] data: The address of the application data to be sent.
      //    1) [in] len: The size of the data block.
      // Returned value:
      //    Actual size of data sent.

   // 将数据放入发送缓冲区
   int send(const char* data, int len);

      // Functionality:
      //    Request UDT to receive data to a memory block "data" with size of "len".
      // Parameters:
      //    0) [out] data: data received.
      //    1) [in] len: The desired size of data to be received.
      // Returned value:
      //    Actual size of data received.

   // 从接收缓冲区中读数据
   int recv(char* data, int len);

      // Functionality:
      //    send a message of a memory block "data" with size of "len".
      // Parameters:
      //    0) [out] data: data received.
      //    1) [in] len: The desired size of data to be received.
      //    2) [in] ttl: the time-to-live of the message.
      //    3) [in] inorder: if the message should be delivered in order.
      // Returned value:
      //    Actual size of data sent.

   // 将数据放入发送缓冲区，相较于send,允许设置消息的生存时间和是否顺序发送
   int sendmsg(const char* data, int len, int ttl, bool inorder);

      // Functionality:
      //    Receive a message to buffer "data".
      // Parameters:
      //    0) [out] data: data received.
      //    1) [in] len: size of the buffer.
      // Returned value:
      //    Actual size of data received.

   // 从接收缓冲区中读取数据
   int recvmsg(char* data, int len);

      // Functionality:
      //    Request UDT to send out a file described as "fd", starting from "offset", with size of "size".
      // Parameters:
      //    0) [in] ifs: The input file stream.
      //    1) [in, out] offset: From where to read and send data; output is the new offset when the call returns.
      //    2) [in] size: How many data to be sent.
      //    3) [in] block: size of block per read from disk
      // Returned value:
      //    Actual size of data sent.

   // 发送文件，按块发送
   int64_t sendfile(std::fstream& ifs, int64_t& offset, int64_t size, int block = 366000);

      // Functionality:
      //    Request UDT to receive data into a file described as "fd", starting from "offset", with expected size of "size".
      // Parameters:
      //    0) [out] ofs: The output file stream.
      //    1) [in, out] offset: From where to write data; output is the new offset when the call returns.
      //    2) [in] size: How many data to be received.
      //    3) [in] block: size of block per write to disk
      // Returned value:
      //    Actual size of data received.

   // 接收文件
   int64_t recvfile(std::fstream& ofs, int64_t& offset, int64_t size, int block = 7320000);

      // Functionality:
      //    Configure UDT options.
      // Parameters:
      //    0) [in] optName: The enum name of a UDT option.
      //    1) [in] optval: The value to be set.
      //    2) [in] optlen: size of "optval".
      // Returned value:
      //    None.

   // 设置套接字参数
   void setOpt(UDTOpt optName, const void* optval, int optlen);

      // Functionality:
      //    Read UDT options.
      // Parameters:
      //    0) [in] optName: The enum name of a UDT option.
      //    1) [in] optval: The value to be returned.
      //    2) [out] optlen: size of "optval".
      // Returned value:
      //    None.

   // 获取套接字参数
   void getOpt(UDTOpt optName, void* optval, int& optlen);

      // Functionality:
      //    read the performance data since last sample() call.
      // Parameters:
      //    0) [in, out] perf: pointer to a CPerfMon structure to record the performance data.
      //    1) [in] clear: flag to decide if the local performance trace should be cleared.
      // Returned value:
      //    None.

   // 传输状态，性能监测
   void sample(CPerfMon* perf, bool clear = true);

private:
   static CUDTUnited s_UDTUnited;               // UDT global management base

public:
   //非法套接字，仅用于初始化
   static const UDTSOCKET INVALID_SOCK;         // invalid socket descriptor
   static const int ERROR;                      // socket api error returned value

private: // Identification
   UDTSOCKET m_SocketID;                        // UDT socket number
   UDTSockType m_iSockType;                     // Type of the UDT connection (SOCK_STREAM or SOCK_DGRAM)
   UDTSOCKET m_PeerID;				// peer id, for multiplexer
   static const int m_iVersion;                 // UDT version, for compatibility use

private: // Packet sizes
   // 默认是: 最大报文段　- (20bytes的IP头+8bytes的UDP头)
   int m_iPktSize;                              // Maximum/regular packet size, in bytes
   // 负载数据大小，不包含包头
   int m_iPayloadSize;                          // Maximum/regular payload size, in bytes

private: // Options
   // 最大报文段,bytes，默认1500
   int m_iMSS;                                  // Maximum Segment Size, in bytes
   // 同步发送模式,阻塞模式
   bool m_bSynSending;                          // Sending syncronization mode
   // 同步接收模式,难道是阻塞模式
   bool m_bSynRecving;                          // Receiving syncronization mode
   int m_iFlightFlagSize;                       // Maximum number of packets in flight from the peer side
   // 发送缓冲区最大容量
   int m_iSndBufSize;                           // Maximum UDT sender buffer size
   int m_iRcvBufSize;                           // Maximum UDT receiver buffer size
   linger m_Linger;                             // Linger information on close
   int m_iUDPSndBufSize;                        // UDP sending buffer size
   int m_iUDPRcvBufSize;                        // UDP receiving buffer size
   int m_iIPversion;                            // IP version
   // 什么是交汇连接模式,难道是P2P模式？
   bool m_bRendezvous;                          // Rendezvous connection mode
   // 发送超时，未设置这个超时时间时，发送会一直阻塞，直到条件变量被唤醒；即阻塞发送
   int m_iSndTimeOut;                           // sending timeout in milliseconds
   // 接收超时，m_iRcvTimeOut < 0说明是阻塞接收
   int m_iRcvTimeOut;                           // receiving timeout in milliseconds
   // 端口复用
   bool m_bReuseAddr;				// reuse an exiting port or not, for UDP multiplexer
   int64_t m_llMaxBW;				// maximum data transfer rate (threshold)

private: // congestion control
   // 拥塞控制工厂类
   CCCVirtualFactory* m_pCCFactory;             // Factory class to create a specific CC instance
   // 拥塞控制类
   CCC* m_pCC;                                  // congestion control class
   // 网络状态缓存
   CCache<CInfoBlock>* m_pCache;		// network information cache

private: // Status
   // 是否处于listening状态
   volatile bool m_bListening;                  // If the UDT entit is listening to connection
   // 正在连接，尚未完成
   volatile bool m_bConnecting;			// The short phase when connect() is called but not yet completed
   // 连接成功
   volatile bool m_bConnected;                  // Whether the connection is on or off
   volatile bool m_bClosing;                    // If the UDT entity is closing
   volatile bool m_bShutdown;                   // If the peer side has shutdown the connection
   volatile bool m_bBroken;                     // If the connection has been broken
   // 对端的连接是否正常
   volatile bool m_bPeerHealth;                 // If the peer status is normal
   // UDT sockfd初始化完成
   bool m_bOpened;                              // If the UDT entity has been opened
   int m_iBrokenCounter;			// a counter (number of GC checks) to let the GC tag this socket as disconnected

   // 超时计时器
   int m_iEXPCount;                             // Expiration counter
   // 估算的带宽，每秒有多少个包
   int m_iBandwidth;                            // Estimated bandwidth, number of packets per second
   // RTT, ms
   int m_iRTT;                                  // RTT, in microseconds
   // RTT变化幅度
   int m_iRTTVar;                               // RTT variance
   // 数据包接收速率-单位时间内接收的数据包数量
   int m_iDeliveryRate;				// Packet arrival rate at the receiver side

   // 在关闭一个仍有未发送数据的sockfd时，等待的时间
   uint64_t m_ullLingerExpiration;		// Linger expiration time (for GC to close a socket with data in sending buffer)

   // 主动建立连接的握手报文
   CHandShake m_ConnReq;			// connection request
   // 对端返回的握手报文
   CHandShake m_ConnRes;			// connection response
   // 用于控制连接请求的发送间隔，避免频繁发送，每250ms最多发送1个请求
   int64_t m_llLastReqTime;			// last time when a connection request is sent

private: // Sending related data
   // 发送缓冲区
   CSndBuffer* m_pSndBuffer;                    // Sender buffer
   // 记录丢的包，用于重传
   CSndLossList* m_pSndLossList;                // Sender loss list
   // 计算发送速度和带宽
   CPktTimeWindow* m_pSndTimeWindow;            // Packet sending time window

   volatile uint64_t m_ullInterval;             // Inter-packet time, in CPU clock cycles
   // 当前时间与计划发送时间的差值，用于在发送数据时进行时间调度，确保数据在适当的时间发送
   uint64_t m_ullTimeDiff;                      // aggregate difference in inter-packet time

   volatile int m_iFlowWindowSize;              // Flow control window size
   volatile double m_dCongestionWindow;         // congestion window size

   volatile int32_t m_iSndLastAck;              // Last ACK received
   volatile int32_t m_iSndLastDataAck;          // The real last ACK that updates the sender buffer and loss list
   volatile int32_t m_iSndCurrSeqNo;            // The largest sequence number that has been sent
   int32_t m_iLastDecSeq;                       // Sequence number sent last decrease occurs
   int32_t m_iSndLastAck2;                      // Last ACK2 sent back
   uint64_t m_ullSndLastAck2Time;               // The time when last ACK2 was sent back

   int32_t m_iISN;                              // Initial Sequence Number

   void CCUpdate();

private: // Receiving related data
   // 接收缓冲区
   CRcvBuffer* m_pRcvBuffer;                    // Receiver buffer
   // 接收端的丢包列表
   CRcvLossList* m_pRcvLossList;                // Receiver loss list
   // 滑动窗口
   CACKWindow* m_pACKWindow;                    // ACK history window
   // 计算接收速度和带宽
   CPktTimeWindow* m_pRcvTimeWindow;            // Packet arrival time window

   int32_t m_iRcvLastAck;                       // Last sent ACK
   // 最新的ACK时间戳
   uint64_t m_ullLastAckTime;                   // Timestamp of last ACK
   int32_t m_iRcvLastAckAck;                    // Last sent ACK that has been acknowledged
   // 最新的ACK序列号
   int32_t m_iAckSeqNo;                         // Last ACK sequence number
   int32_t m_iRcvCurrSeqNo;                     // Largest received sequence number

   uint64_t m_ullLastWarningTime;               // Last time that a warning message is sent

   int32_t m_iPeerISN;                          // Initial Sequence Number of the peer side

private: // synchronization: mutexes and conditions
   // 连接参数保护
   pthread_mutex_t m_ConnectionLock;            // used to synchronize connection operation

   pthread_cond_t m_SendBlockCond;              // used to block "send" call
   // 发送数据时保护临界区
   pthread_mutex_t m_SendBlockLock;             // lock associated to m_SendBlockCond

   pthread_mutex_t m_AckLock;                   // used to protected sender's loss list when processing ACK

   // 接收数据时使用的条件变量和锁
   pthread_cond_t m_RecvDataCond;               // used to block "recv" when there is no data
   pthread_mutex_t m_RecvDataLock;              // lock associated to m_RecvDataCond

   pthread_mutex_t m_SendLock;                  // used to synchronize "send" call
   pthread_mutex_t m_RecvLock;                  // used to synchronize "recv" call

   void initSynch();
   void destroySynch();
   void releaseSynch();

private: // Generation and processing of packets
   void sendCtrl(int pkttype, void* lparam = NULL, void* rparam = NULL, int size = 0);
   void processCtrl(CPacket& ctrlpkt);
   int packData(CPacket& packet, uint64_t& ts);
   int processData(CUnit* unit);
   int listen(sockaddr* addr, CPacket& packet);

private: // Trace
   // 一个UDT实例启动时的时间戳
   uint64_t m_StartTime;                        // timestamp when the UDT entity is started
   // 发送数据包总数，包含重传的包
   int64_t m_llSentTotal;                       // total number of sent data packets, including retransmissions
   // 接受数据包总数
   int64_t m_llRecvTotal;                       // total number of received packets
   int m_iSndLossTotal;                         // total number of lost packets (sender side)
   // 接收侧丢包统计
   int m_iRcvLossTotal;                         // total number of lost packets (receiver side)
   int m_iRetransTotal;                         // total number of retransmitted packets
   int m_iSentACKTotal;                         // total number of sent ACK packets
   int m_iRecvACKTotal;                         // total number of received ACK packets
   int m_iSentNAKTotal;                         // total number of sent NAK packets
   int m_iRecvNAKTotal;                         // total number of received NAK packets
   int64_t m_llSndDurationTotal;		// total real time for sending

   // 最后一次统计状态时的时间戳
   uint64_t m_LastSampleTime;                   // last performance sample time
   // 最近一个统计周期内，发送数据包的数量
   int64_t m_llTraceSent;                       // number of pakctes sent in the last trace interval
   // 最近一个统计周期内，接收数据包的数量
   int64_t m_llTraceRecv;                       // number of pakctes received in the last trace interval
   int m_iTraceSndLoss;                         // number of lost packets in the last trace interval (sender side)
   // 最近一个统计收起内，丢包数量
   int m_iTraceRcvLoss;                         // number of lost packets in the last trace interval (receiver side)
   int m_iTraceRetrans;                         // number of retransmitted packets in the last trace interval
   int m_iSentACK;                              // number of ACKs sent in the last trace interval
   int m_iRecvACK;                              // number of ACKs received in the last trace interval
   int m_iSentNAK;                              // number of NAKs sent in the last trace interval
   int m_iRecvNAK;                              // number of NAKs received in the last trace interval
   // 上一次统计到最新一次统计间隔的时间
   int64_t m_llSndDuration;			// real time for sending
   // 记录发送数据用了多长时间
   int64_t m_llSndDurationCounter;		// timers to record the sending duration

private: // Timers
   // CPU时钟频率，单位KHz
   uint64_t m_ullCPUFrequency;                  // CPU clock frequency, used for Timer, ticks per microsecond

   // 用于速率控制
   static const int m_iSYNInterval;             // Periodical Rate Control Interval, 10000 microsecond
   static const int m_iSelfClockInterval;       // ACK interval for self-clocking

   uint64_t m_ullNextACKTime;			// Next ACK time, in CPU clock cycles, same below
   uint64_t m_ullNextNAKTime;			// Next NAK time

   // SYN包的时间间隔
   volatile uint64_t m_ullSYNInt;		// SYN interval
   // ACK包的时间间隔
   volatile uint64_t m_ullACKInt;		// ACK interval
   // NACK包的时间间隔
   volatile uint64_t m_ullNAKInt;		// NAK interval
   // 对端相应的时间戳，用于超时检测
   volatile uint64_t m_ullLastRspTime;		// time stamp of last response from the peer

   // NACK超时下限，超时将重传
   uint64_t m_ullMinNakInt;			// NAK timeout lower bound; too small value can cause unnecessary retransmission
   // 超时时间下限
   uint64_t m_ullMinExpInt;			// timeout lower bound threshold: too small timeout can cause problem

   // ACK包统计
   int m_iPktCount;				// packet counter for ACK
   int m_iLightACKCount;			// light ACK counter

   // 下一个数据包的调度时间
   uint64_t m_ullTargetTime;			// scheduled time of next packet sending

   void checkTimers();

private: // for UDP multiplexer
   // 发送队列
   CSndQueue* m_pSndQueue;			// packet sending queue
   // 接收队列
   CRcvQueue* m_pRcvQueue;			// packet receiving queue
   // 对端地址
   sockaddr* m_pPeerAddr;			// peer address
   // 本地IP
   uint32_t m_piSelfIP[4];			// local UDP IP address
   // 发送队列节点指针
   CSNode* m_pSNode;				// node information for UDT list used in snd queue
   // 接收队列节点指针
   CRNode* m_pRNode;                            // node information for UDT list used in rcv queue

private: // for epoll
   std::set<int> m_sPollID;                     // set of epoll ID to trigger
   void addEPoll(const int eid);
   void removeEPoll(const int eid);
};


#endif
