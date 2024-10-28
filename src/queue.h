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
   Yunhong Gu, last updated 01/12/2011
*****************************************************************************/


#ifndef __UDT_QUEUE_H__
#define __UDT_QUEUE_H__

#include "channel.h"
#include "common.h"
#include "packet.h"
#include <list>
#include <map>
#include <queue>
#include <vector>

class CUDT;

// 数据包及状态：0：空闲，1：已占用，2：已读取但未释放（乱序），3：丢弃MSG
struct CUnit
{
   CPacket m_Packet;		// packet
   int m_iFlag;			// 0: free, 1: occupied, 2: msg read but not freed (out-of-order), 3: msg dropped
};

// 使用一个循环队列来存储数据包，队列中的每个节点都是一个固定容量的packet队列
// 堆空间扩容就是增加一个固定容量的packet队列
class CUnitQueue
{
friend class CRcvQueue;
friend class CRcvBuffer;

public:
   CUnitQueue();
   ~CUnitQueue();

public:

      // Functionality:
      //    Initialize the unit queue.
      // Parameters:
      //    1) [in] size: queue size
      //    2) [in] mss: maximum segament size
      //    3) [in] version: IP version
      // Returned value:
      //    0: success, -1: failure.

   // 初始化，开辟堆空间，创建一个循环队列
   int init(int size, int mss, int version);

      // Functionality:
      //    Increase (double) the unit queue size.
      // Parameters:
      //    None.
      // Returned value:
      //    0: success, -1: failure.

   // 增加堆空间容量
   int increase();

      // Functionality:
      //    Decrease (halve) the unit queue size.
      // Parameters:
      //    None.
      // Returned value:
      //    0: success, -1: failure.

   // 缩减堆空间容量，暂未实现
   int shrink();

      // Functionality:
      //    find an available unit for incoming packet.
      // Parameters:
      //    None.
      // Returned value:
      //    Pointer to the available unit, NULL if not found.

   // 找到一个空闲的数据单元，用来存储packet
   CUnit* getNextAvailUnit();

private:
   struct CQEntry          // CQEntry == Class Queue Entry
   {
      CUnit* m_pUnit;		// 队列元素指针，unit queue
      char* m_pBuffer;		// 队列中节点的指针域，指向堆空间，data buffer
      int m_iSize;		   // 队列中共有多少个节点，size of each queue

      CQEntry* m_pNext;
   }
   // 队列入口，是一个循环队列
   *m_pQEntry,			// pointer to the first unit queue
   // 指向当前元素
   *m_pCurrQueue,		// pointer to the current available queue
   // 指向队列中的最后一个元素
   *m_pLastQueue;		// pointer to the last unit queue

   // 最新的一个可用数据单元
   CUnit* m_pAvailUnit;         // recent available unit

   // 队列中的packet总数
   int m_iSize;			// total size of the unit queue, in number of packets
   // 有多少数据单元处于非空闲状态
   int m_iCount;		// total number of valid packets in the queue

   // // 队列中每个packet的最大长度
   int m_iMSS;			// unit buffer size
   // IPv4 or IPv6
   int m_iIPversion;		// IP version

private:
   // 仅声明未实现，相当于禁用拷贝赋值
   CUnitQueue(const CUnitQueue&);
   // 仅声明未实现，相当于赋值运算符
   CUnitQueue& operator=(const CUnitQueue&);
};

struct CSNode
{
   // 关联的CUDT实例
   CUDT* m_pUDT;		// Pointer to the instance of CUDT socket
   // 时间戳
   uint64_t m_llTimeStamp;      // Time Stamp

   // 在堆中的位置
   int m_iHeapLoc;		// location on the heap, -1 means not on the heap
};

// 使用一个小根堆来存储待发送的数据，堆中的节点安装时间戳进行排序，堆顶节点一定是时间戳最小的节点
// 小根堆的大小可以动态增大，调整方式是成倍增加
class CSndUList
{
friend class CSndQueue;

public:
   CSndUList();
   ~CSndUList();

public:

      // Functionality:
      //    Insert a new UDT instance into the list.
      // Parameters:
      //    1) [in] ts: time stamp: next processing time
      //    2) [in] u: pointer to the UDT instance
      // Returned value:
      //    None.
   
   // 向堆中插入一个新的UDT实例
   void insert(int64_t ts, const CUDT* u);

      // Functionality:
      //    Update the timestamp of the UDT instance on the list.
      // Parameters:
      //    1) [in] u: pointer to the UDT instance
      //    2) [in] resechedule: if the timestampe shoudl be rescheduled
      // Returned value:
      //    None.

   // 更新UDT实例的时间戳，有什么用？
   void update(const CUDT* u, bool reschedule = true);

      // Functionality:
      //    Retrieve the next packet and peer address from the first entry, and reschedule it in the queue.
      // Parameters:
      //    0) [out] addr: destination address of the next packet
      //    1) [out] pkt: the next packet to be sent
      // Returned value:
      //    1 if successfully retrieved, -1 if no packet found.

   int pop(sockaddr*& addr, CPacket& pkt);

      // Functionality:
      //    Remove UDT instance from the list.
      // Parameters:
      //    1) [in] u: pointer to the UDT instance
      // Returned value:
      //    None.

   // 删除UDT实例
   void remove(const CUDT* u);

      // Functionality:
      //    Retrieve the next scheduled processing time.
      // Parameters:
      //    None.
      // Returned value:
      //    Scheduled processing time of the first UDT socket in the list.

   // 获取堆顶元素的时间戳
   uint64_t getNextProcTime();

private:
   // 插入数据到堆中
   void insert_(int64_t ts, const CUDT* u);
   // 从堆中删除数据
   void remove_(const CUDT* u);

private:
   // 待发送数据，使用一个小根堆来实现
   CSNode** m_pHeap;			// The heap array
   // 堆中的节点个数
   int m_iArrayLength;			// physical length of the array
   // 最新一个节点的位置
   int m_iLastEntry;			// position of last entry on the heap array

   // 同步访问保护
   pthread_mutex_t m_ListLock;

   pthread_mutex_t* m_pWindowLock;

   pthread_cond_t* m_pWindowCond;

   // 定时器的初始化由class CSndQueue来完成
   CTimer* m_pTimer;

private:
   CSndUList(const CSndUList&);
   CSndUList& operator=(const CSndUList&);
};

struct CRNode
{
   CUDT* m_pUDT;                // Pointer to the instance of CUDT socket
   uint64_t m_llTimeStamp;      // Time Stamp

   CRNode* m_pPrev;             // previous link
   CRNode* m_pNext;             // next link

   bool m_bOnList;              // if the node is already on the list
};

class CRcvUList
{
public:
   CRcvUList();
   ~CRcvUList();

public:

      // Functionality:
      //    Insert a new UDT instance to the list.
      // Parameters:
      //    1) [in] u: pointer to the UDT instance
      // Returned value:
      //    None.

   void insert(const CUDT* u);

      // Functionality:
      //    Remove the UDT instance from the list.
      // Parameters:
      //    1) [in] u: pointer to the UDT instance
      // Returned value:
      //    None.

   void remove(const CUDT* u);

      // Functionality:
      //    Move the UDT instance to the end of the list, if it already exists; otherwise, do nothing.
      // Parameters:
      //    1) [in] u: pointer to the UDT instance
      // Returned value:
      //    None.

   void update(const CUDT* u);

public:
   CRNode* m_pUList;		// the head node

private:
   CRNode* m_pLast;		// the last node

private:
   CRcvUList(const CRcvUList&);
   CRcvUList& operator=(const CRcvUList&);
};

class CHash
{
public:
   CHash();
   ~CHash();

public:

      // Functionality:
      //    Initialize the hash table.
      // Parameters:
      //    1) [in] size: hash table size
      // Returned value:
      //    None.

   void init(int size);

      // Functionality:
      //    Look for a UDT instance from the hash table.
      // Parameters:
      //    1) [in] id: socket ID
      // Returned value:
      //    Pointer to a UDT instance, or NULL if not found.

   CUDT* lookup(int32_t id);

      // Functionality:
      //    Insert an entry to the hash table.
      // Parameters:
      //    1) [in] id: socket ID
      //    2) [in] u: pointer to the UDT instance
      // Returned value:
      //    None.

   void insert(int32_t id, CUDT* u);

      // Functionality:
      //    Remove an entry from the hash table.
      // Parameters:
      //    1) [in] id: socket ID
      // Returned value:
      //    None.

   void remove(int32_t id);

private:
   struct CBucket
   {
      int32_t m_iID;		// Socket ID
      CUDT* m_pUDT;		// Socket instance

      CBucket* m_pNext;		// next bucket
   } **m_pBucket;		// list of buckets (the hash table)

   int m_iHashSize;		// size of hash table

private:
   CHash(const CHash&);
   CHash& operator=(const CHash&);
};

// 使用一个list来维护处于rendezvous模式的UDT实例
class CRendezvousQueue
{
public:
   CRendezvousQueue();
   ~CRendezvousQueue();

public:
   // 插入一个UDT实例
   void insert(const UDTSOCKET& id, CUDT* u, int ipv, const sockaddr* addr, uint64_t ttl);
   // 删除一个UDT实例
   void remove(const UDTSOCKET& id);
   // 检索一个UDT实例
   CUDT* retrieve(const sockaddr* addr, UDTSOCKET& id);

   // 更新连接状态
   void updateConnStatus();

private:
   struct CRL
   {
      // UDT套接字
      UDTSOCKET m_iID;			// UDT socket ID (self)
      // UDT实例句柄
      CUDT* m_pUDT;			// UDT instance
      // IPv4/IPv6
      int m_iIPversion;                 // IP version
      // 对端地址
      sockaddr* m_pPeerAddr;		// UDT sonnection peer address
      // 请求过期时间
      uint64_t m_ullTTL;			// the time that this request expires
   };
   // 处于rendezvous模式的UDT实例list
   std::list<CRL> m_lRendezvousID;      // The sockets currently in rendezvous mode

   pthread_mutex_t m_RIDVectorLock;
};

class CSndQueue
{
friend class CUDT;
friend class CUDTUnited;

public:
   CSndQueue();
   ~CSndQueue();

public:

      // Functionality:
      //    Initialize the sending queue.
      // Parameters:
      //    1) [in] c: UDP channel to be associated to the queue
      //    2) [in] t: Timer
      // Returned value:
      //    None.

   void init(CChannel* c, CTimer* t);

      // Functionality:
      //    Send out a packet to a given address.
      // Parameters:
      //    1) [in] addr: destination address
      //    2) [in] packet: packet to be sent out
      // Returned value:
      //    Size of data sent out.

   int sendto(const sockaddr* addr, CPacket& packet);

private:
#ifndef WIN32
   static void* worker(void* param);
#else
   static DWORD WINAPI worker(LPVOID param);
#endif

   pthread_t m_WorkerThread;

private:
   // 使用一个小根堆来存储待发送的数据，堆中的节点按时间戳进行排序
   CSndUList* m_pSndUList;		// List of UDT instances for data sending
   CChannel* m_pChannel;                // The UDP channel for data sending
   CTimer* m_pTimer;			// Timing facility

   pthread_mutex_t m_WindowLock;
   pthread_cond_t m_WindowCond;

   volatile bool m_bClosing;		// closing the worker
   pthread_cond_t m_ExitCond;

private:
   CSndQueue(const CSndQueue&);
   CSndQueue& operator=(const CSndQueue&);
};

class CRcvQueue
{
friend class CUDT;
friend class CUDTUnited;

public:
   CRcvQueue();
   ~CRcvQueue();

public:

      // Functionality:
      //    Initialize the receiving queue.
      // Parameters:
      //    1) [in] size: queue size
      //    2) [in] mss: maximum packet size
      //    3) [in] version: IP version
      //    4) [in] hsize: hash table size
      //    5) [in] c: UDP channel to be associated to the queue
      //    6) [in] t: timer
      // Returned value:
      //    None.

   // 初始化接收队列
   void init(int size, int payload, int version, int hsize, CChannel* c, CTimer* t);

      // Functionality:
      //    Read a packet for a specific UDT socket id.
      // Parameters:
      //    1) [in] id: Socket ID
      //    2) [out] packet: received packet
      // Returned value:
      //    Data size of the packet

   // 接收一个packet
   int recvfrom(int32_t id, CPacket& packet);

private:
#ifndef WIN32
   // 用于处理数据接收的工作线程
   static void* worker(void* param);
#else
   static DWORD WINAPI worker(LPVOID param);
#endif

   // 用于处理数据接收的工作线程ID
   pthread_t m_WorkerThread;

private:
   // 存储接收到数据包的队列
   CUnitQueue m_UnitQueue;		// The received packet queue

   // UDT实例列表
   CRcvUList* m_pRcvUList;		// List of UDT instances that will read packets from the queue
   // 用于查找UDT套接字
   CHash* m_pHash;			// Hash table for UDT socket looking up
   // 关联的UDP通道
   CChannel* m_pChannel;		// UDP channel for receving packets
   // 定时器
   CTimer* m_pTimer;			// shared timer with the snd queue

   // 数据包负载大小
   int m_iPayloadSize;                  // packet payload size

   // 工作线程是否正在关闭
   volatile bool m_bClosing;            // closing the workder
   pthread_cond_t m_ExitCond;

private:
   // listener
   int setListener(CUDT* u);
   void removeListener(const CUDT* u);

   // connector
   void registerConnector(const UDTSOCKET& id, CUDT* u, int ipv, const sockaddr* addr, uint64_t ttl);
   void removeConnector(const UDTSOCKET& id);

   // 
   void setNewEntry(CUDT* u);
   bool ifNewEntry();
   CUDT* getNewEntry();

   // 保存packet
   void storePkt(int32_t id, CPacket* pkt);

private:
   // 同步访问，临界区保护
   pthread_mutex_t m_LSLock;
   // 指向监听的UDT实例
   CUDT* m_pListener;                                   // pointer to the (unique, if any) listening UDT entity
   // 管理交汇连接模式的队列，P2P模式
   CRendezvousQueue* m_pRendezvousQueue;                // The list of sockets in rendezvous mode

   // 存储新的UDT实例
   std::vector<CUDT*> m_vNewEntry;                      // newly added entries, to be inserted
   pthread_mutex_t m_IDLock;

   std::map<int32_t, std::queue<CPacket*> > m_mBuffer;	// temporary buffer for rendezvous connection request
   pthread_mutex_t m_PassLock;
   pthread_cond_t m_PassCond;

private:
   CRcvQueue(const CRcvQueue&);
   CRcvQueue& operator=(const CRcvQueue&);
};

// CMultiplexer，UDP连接复用器，每一个CMultiplexer都是一个已建立的UDP连接
// 描述了一个UDT套接字相关的发送/接收队列,端口号等参数
struct CMultiplexer
{
   // 发送队列，存储待发送的数据
   CSndQueue* m_pSndQueue;	// The sending queue
   // 接收队列，存储接收到的数据
   CRcvQueue* m_pRcvQueue;	// The receiving queue
   // UDP channel
   CChannel* m_pChannel;	// The UDP channel for sending and receiving
   // 定时器
   CTimer* m_pTimer;		// The timer

   // UDP端口号
   int m_iPort;			// The UDP port number of this multiplexer
   // IPv4 or IPv6
   int m_iIPversion;		// IP version
   // 最大报文段大小
   int m_iMSS;			// Maximum Segment Size
   // UDT实例个数
   int m_iRefCount;		// number of UDT instances that are associated with this multiplexer
   // 地址是否可以复用
   bool m_bReusable;		// if this one can be shared with others

   int m_iID;			// multiplexer ID
};

#endif
