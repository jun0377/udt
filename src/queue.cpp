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
   Yunhong Gu, last updated 05/05/2011
*****************************************************************************/

#ifdef WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #ifdef LEGACY_WIN32
      #include <wspiapi.h>
   #endif
#endif
#include <cstring>

#include "common.h"
#include "core.h"
#include "queue.h"

using namespace std;

CUnitQueue::CUnitQueue():
m_pQEntry(NULL),
m_pCurrQueue(NULL),
m_pLastQueue(NULL),
m_iSize(0),
m_iCount(0),
m_iMSS(),
m_iIPversion()
{
}

CUnitQueue::~CUnitQueue()
{
   CQEntry* p = m_pQEntry;

   while (p != NULL)
   {
      delete [] p->m_pUnit;
      delete [] p->m_pBuffer;

      CQEntry* q = p;
      if (p == m_pLastQueue)
         p = NULL;
      else
         p = p->m_pNext;
      delete q;
   }
}

int CUnitQueue::init(int size, int mss, int version)
{
   // 入口
   CQEntry* tempq = NULL;
   // 数据单元队列
   CUnit* tempu = NULL;
   // 真正存储数据的堆空间
   char* tempb = NULL;

   try
   {
      // CUnitQueue入口
      tempq = new CQEntry;
      // CUnitQueue中的数据单元
      tempu = new CUnit [size];
      // 真正存储数据的堆空间
      tempb = new char [size * mss];
   }
   catch (...)
   {
      delete tempq;
      delete [] tempu;
      delete [] tempb;

      return -1;
   }

   // 初始化队列
   for (int i = 0; i < size; ++ i)
   {
      // 初始化所有数据单元为空闲状态
      tempu[i].m_iFlag = 0;
      // 初始化所有数据单元占用的堆空间地址，基地址 + 偏移量
      tempu[i].m_Packet.m_pcData = tempb + i * mss;
   }
   // 队列入口
   tempq->m_pUnit = tempu;          // 队列入口指针
   tempq->m_pBuffer = tempb;        // 真正存储数据的堆空间
   tempq->m_iSize = size;           // 队列中共有多少个节点

   // 队列入口 = 当前队列 = 最后一个队列
   m_pQEntry = m_pCurrQueue = m_pLastQueue = tempq;
   // 循环队列
   m_pQEntry->m_pNext = m_pQEntry;

   // 第一个可用的数据单元
   m_pAvailUnit = m_pCurrQueue->m_pUnit;

    // 队列中的数据单元总数
   m_iSize = size;
   // 队列中每个数据单元负载的最大长度
   m_iMSS = mss;
   // IPv4/IPv6
   m_iIPversion = version;

   return 0;
}

int CUnitQueue::increase()
{
   // adjust/correct m_iCount
   // 统计有多少数据单元已被占用
   int real_count = 0;
   // CUnitQueue队列入口
   CQEntry* p = m_pQEntry;
   // 遍历队列，统计处于有多少数据单元出于非空闲状态
   while (p != NULL)
   {
      // 队列中的数据单元
      CUnit* u = p->m_pUnit;
      // 遍历队列中的数据单元
      for (CUnit* end = u + p->m_iSize; u != end; ++ u)
         // 统计有多少数据单元处于非空闲状态
         if (u->m_iFlag != 0)
            ++ real_count;

      // 所有队列都遍历完成
      if (p == m_pLastQueue)
         p = NULL;
      // 当前队列统计完成，切换到下一个队列
      else
         p = p->m_pNext;
   }
   // 有多少节点处于非空闲状态
   m_iCount = real_count;
   // 如果队列中处于非空闲状态的数据单元比例小于90%，则认为不需要增加队列容量
   if (double(m_iCount) / m_iSize < 0.9)
      return -1;

   CQEntry* tempq = NULL;
   CUnit* tempu = NULL;
   char* tempb = NULL;

   // all queues have the same size
   // 每个数据单元队列的容量，所有的队列容量都相同，都有m_iSize个数据单元
   int size = m_pQEntry->m_iSize;

   // 创建一个新的数据单元队列
   try
   {
      tempq = new CQEntry;
      tempu = new CUnit [size];
      tempb = new char [size * m_iMSS];
   }
   catch (...)
   {
      delete tempq;
      delete [] tempu;
      delete [] tempb;

      return -1;
   }

   for (int i = 0; i < size; ++ i)
   {
      // 初始化所有数据单元为空间状态
      tempu[i].m_iFlag = 0;
      // 初始化所有数据单元的堆空间
      tempu[i].m_Packet.m_pcData = tempb + i * m_iMSS;
   }
   tempq->m_pUnit = tempu;
   tempq->m_pBuffer = tempb;
   tempq->m_iSize = size;

   // 将新建的数据单元队列纳入到CUnitQueue中
   m_pLastQueue->m_pNext = tempq;
   m_pLastQueue = tempq;
   m_pLastQueue->m_pNext = m_pQEntry;

   // CUnitQueue容量
   m_iSize += size;

   return 0;
}

int CUnitQueue::shrink()
{
   // currently queue cannot be shrunk.
   return -1;
}

// 获取一个空闲的数据单元
CUnit* CUnitQueue::getNextAvailUnit()
{
   // m_iCount > 0.9 * m_iSize; 即堆空间的使用率超过了90%，需要扩容
   if (m_iCount * 10 > m_iSize * 9)
      increase();

   // 堆空间已满
   if (m_iCount >= m_iSize)
      return NULL;

   // 队列入口
   CQEntry* entrance = m_pCurrQueue;

   do
   {
      // 从m_pAvailUnit开始，遍历队列，找到一个空闲的数据单元
      for (CUnit* sentinel = m_pCurrQueue->m_pUnit + m_pCurrQueue->m_iSize - 1; m_pAvailUnit != sentinel; ++ m_pAvailUnit)
         if (m_pAvailUnit->m_iFlag == 0)
            return m_pAvailUnit;

      // 检查当前队列的第一个数据单元是否可用
      if (m_pCurrQueue->m_pUnit->m_iFlag == 0)
      {
         m_pAvailUnit = m_pCurrQueue->m_pUnit;
         return m_pAvailUnit;
      }

      // 当前队列中没有可用的数据单元，遍历下一个队列
      m_pCurrQueue = m_pCurrQueue->m_pNext;
      m_pAvailUnit = m_pCurrQueue->m_pUnit;
   } while (m_pCurrQueue != entrance);

   // 没有找到可用的数据单元，需要扩容
   increase();

   return NULL;
}


CSndUList::CSndUList():
m_pHeap(NULL),
m_iArrayLength(4096),
m_iLastEntry(-1),
m_ListLock(),
m_pWindowLock(NULL),
m_pWindowCond(NULL),
m_pTimer(NULL)
{
   m_pHeap = new CSNode*[m_iArrayLength];

   #ifndef WIN32
      pthread_mutex_init(&m_ListLock, NULL);
   #else
      m_ListLock = CreateMutex(NULL, false, NULL);
   #endif
}

CSndUList::~CSndUList()
{
   delete [] m_pHeap;

   #ifndef WIN32
      pthread_mutex_destroy(&m_ListLock);
   #else
      CloseHandle(m_ListLock);
   #endif
}

void CSndUList::insert(int64_t ts, const CUDT* u)
{
   // lock_guard
   CGuard listguard(m_ListLock);

   // increase the heap array size if necessary
   // 堆已满，动态增加堆空间，成倍增加
   if (m_iLastEntry == m_iArrayLength - 1)
   {
      CSNode** temp = NULL;

      try
      {
         temp = new CSNode*[m_iArrayLength * 2];
      }
      catch(...)
      {
         return;
      }

      memcpy(temp, m_pHeap, sizeof(CSNode*) * m_iArrayLength);
      m_iArrayLength *= 2;
      delete [] m_pHeap;
      m_pHeap = temp;
   }
   
   // 将数据插入到堆中
   insert_(ts, u);
}


// 如果一个UDT实例的待发送数据需要重新调度，则将其调整到小根堆的堆顶，以便尽快处理
void CSndUList::update(const CUDT* u, bool reschedule)
{
   // lock_guard
   CGuard listguard(m_ListLock);

   // 获取UDT实例中的待发送数据
   CSNode* n = u->m_pSNode;

   // n->m_iHeapLoc >= 0说明该UDT实例的待发送数据已经存在于堆中
   if (n->m_iHeapLoc >= 0)
   {
      // 不需要重新调度，直接返回
      if (!reschedule)
         return;

      // 如果UDT实例是堆顶元素，则...
      if (n->m_iHeapLoc == 0)
      {
         n->m_llTimeStamp = 1;
         m_pTimer->interrupt();
         return;
      }

      // 如果UDT实例不是堆顶元素，先将其从堆中删除
      remove_(u);
   }

   // 更新时间戳后，重新将UDT实例放入堆中
   // 将时间戳设置为1意味着该节点需要立即被重新调度
   // 因为在堆中所有其他节点的时间戳都会大于 1，这样可以确保该节点被放置在堆顶位置，以便尽快处理
   insert_(1, u);
}

int CSndUList::pop(sockaddr*& addr, CPacket& pkt)
{
   // lock_guard
   CGuard listguard(m_ListLock);

   // 堆中没有数据，直接返回
   if (-1 == m_iLastEntry)
      return -1;

   // no pop until the next schedulled time
   // 获取当前时间戳
   uint64_t ts;
   CTimer::rdtsc(ts);
   // 如果当前时间戳比发送数据的时间戳小，说明尚未到待发送数据规划的调度时间，直接返回
   if (ts < m_pHeap[0]->m_llTimeStamp){
      return -1;
   }

   // 获取堆顶元素对应的UDT实例
   CUDT* u = m_pHeap[0]->m_pUDT;
   // 删除堆顶元素
   remove_(u);

   // 如果连接异常，说明无法发送数据，直接返回
   if (!u->m_bConnected || u->m_bBroken)
      return -1;

   // pack a packet from the socket
   // 数据打包，并计算下一次调度的时间
   if (u->packData(pkt, ts) <= 0)
      return -1;

   // 取出UDT实例对应的对端地址
   addr = u->m_pPeerAddr;

   // insert a new entry, ts is the next processing time
   // 如果计算得到的 ts 大于 0，表示还有剩余时间，根据下一次调度时间，重新将数据放入堆中
   if (ts > 0)
      insert_(ts, u);

   return 1;
}

void CSndUList::remove(const CUDT* u)
{
   // lock_guard
   CGuard listguard(m_ListLock);

   remove_(u);
}

// 下一次调度时间，即堆顶元素的发送时间戳
uint64_t CSndUList::getNextProcTime()
{
   // lock_guard
   CGuard listguard(m_ListLock);
   
   // 堆为空，直接返回
   if (-1 == m_iLastEntry)
      return 0;

   // 返回堆顶元素时间戳
   return m_pHeap[0]->m_llTimeStamp;
}

void CSndUList::insert_(int64_t ts, const CUDT* u)
{
   // std::cout << "insert_ : ts : " << ts << std::endl;

   // 取出UDT实例中的待发送数据
   CSNode* n = u->m_pSNode;

   // do not insert repeated node
   // 如果待发送数据已经存在于堆中，则直接返回
   if (n->m_iHeapLoc >= 0)
      return;

   // 插入数据到堆中
   m_iLastEntry ++;
   m_pHeap[m_iLastEntry] = n;
   n->m_llTimeStamp = ts;

   // 堆排序，按时间戳进行小根堆排序,堆顶节点一定是时间戳最小的节点
   int q = m_iLastEntry;
   int p = q;
   while (p != 0)
   {
      p = (q - 1) >> 1;
      if (m_pHeap[p]->m_llTimeStamp > m_pHeap[q]->m_llTimeStamp)
      {
         CSNode* t = m_pHeap[p];
         m_pHeap[p] = m_pHeap[q];
         m_pHeap[q] = t;
         t->m_iHeapLoc = q;
         q = p;
      }
      else
         break;
   }

   // 更新堆索引
   n->m_iHeapLoc = q;

   // an earlier event has been inserted, wake up sending worker
   // 新插入的数据比堆顶的时间戳都要早，需要立即发送
   if (n->m_iHeapLoc == 0){
      m_pTimer->interrupt();
   }

   // first entry, activate the sending queue
   // 当堆中没有数据时，唤醒发送线程，执行退出操作
   if (0 == m_iLastEntry)
   {
      #ifndef WIN32
         pthread_mutex_lock(m_pWindowLock);
         pthread_cond_signal(m_pWindowCond);
         pthread_mutex_unlock(m_pWindowLock);
      #else
         SetEvent(*m_pWindowCond);
      #endif
   }
}

void CSndUList::remove_(const CUDT* u)
{
   // 获取UDT实例待发送数据
   CSNode* n = u->m_pSNode;

   // UDT实例中的待发送数据仍然在堆中
   if (n->m_iHeapLoc >= 0)
   {
      // remove the node from heap
      // 从堆中删除该节点
      m_pHeap[n->m_iHeapLoc] = m_pHeap[m_iLastEntry];
      m_iLastEntry --;
      m_pHeap[n->m_iHeapLoc]->m_iHeapLoc = n->m_iHeapLoc;

      // 删除元素后重新调整堆结构
      int q = n->m_iHeapLoc;
      int p = q * 2 + 1;
      while (p <= m_iLastEntry)
      {
         if ((p + 1 <= m_iLastEntry) && (m_pHeap[p]->m_llTimeStamp > m_pHeap[p + 1]->m_llTimeStamp))
            p ++;

         if (m_pHeap[q]->m_llTimeStamp > m_pHeap[p]->m_llTimeStamp)
         {
            CSNode* t = m_pHeap[p];
            m_pHeap[p] = m_pHeap[q];
            m_pHeap[p]->m_iHeapLoc = p;
            m_pHeap[q] = t;
            m_pHeap[q]->m_iHeapLoc = q;

            q = p;
            p = q * 2 + 1;
         }
         else
            break;
      }

      // 更新待发送数据在堆中的位置，-1表示不在堆中
      n->m_iHeapLoc = -1;
   }

   // the only event has been deleted, wake up immediately
   // 堆中数据为空？？？
   if (0 == m_iLastEntry)
      m_pTimer->interrupt();
}

//
CSndQueue::CSndQueue():
m_WorkerThread(),
m_pSndUList(NULL),
m_pChannel(NULL),
m_pTimer(NULL),
m_WindowLock(),
m_WindowCond(),
m_bClosing(false),
m_ExitCond()
{
   #ifndef WIN32
      pthread_cond_init(&m_WindowCond, NULL);
      pthread_mutex_init(&m_WindowLock, NULL);
   #else
      m_WindowLock = CreateMutex(NULL, false, NULL);
      m_WindowCond = CreateEvent(NULL, false, false, NULL);
      m_ExitCond = CreateEvent(NULL, false, false, NULL);
   #endif
}

CSndQueue::~CSndQueue()
{
   m_bClosing = true;

   #ifndef WIN32
      pthread_mutex_lock(&m_WindowLock);
      pthread_cond_signal(&m_WindowCond);
      pthread_mutex_unlock(&m_WindowLock);
      if (0 != m_WorkerThread)
         pthread_join(m_WorkerThread, NULL);
      pthread_cond_destroy(&m_WindowCond);
      pthread_mutex_destroy(&m_WindowLock);
   #else
      SetEvent(m_WindowCond);
      if (NULL != m_WorkerThread)
         WaitForSingleObject(m_ExitCond, INFINITE);
      CloseHandle(m_WorkerThread);
      CloseHandle(m_WindowLock);
      CloseHandle(m_WindowCond);
      CloseHandle(m_ExitCond);
   #endif

   delete m_pSndUList;
}

// 初始化，创建了一个发送数据的工作线程
void CSndQueue::init(CChannel* c, CTimer* t)
{
   // 关联UDP通道
   m_pChannel = c;
   // 关联定时器
   m_pTimer = t;
   // 创建发送列表
   m_pSndUList = new CSndUList;
   m_pSndUList->m_pWindowLock = &m_WindowLock;
   m_pSndUList->m_pWindowCond = &m_WindowCond;
   m_pSndUList->m_pTimer = m_pTimer;

   #ifndef WIN32
      // 创建工作线程
      if (0 != pthread_create(&m_WorkerThread, NULL, CSndQueue::worker, this))
      {
         m_WorkerThread = 0;
         throw CUDTException(3, 1);
      }
   #else
      DWORD threadID;
      m_WorkerThread = CreateThread(NULL, 0, CSndQueue::worker, this, 0, &threadID);
      if (NULL == m_WorkerThread)
         throw CUDTException(3, 1);
   #endif
}
/* 
   从SndList中pop数据，通过UDP通道发送到对端
*/
#ifndef WIN32
   // 从SndList中pop数据，通过UDP通道发送到对端
   void* CSndQueue::worker(void* param)
#else
   DWORD WINAPI CSndQueue::worker(LPVOID param)
#endif
{
   CSndQueue* self = (CSndQueue*)param;

   while (!self->m_bClosing)
   {
      // 下一次发送数据的时间，即调度时间
      uint64_t ts = self->m_pSndUList->getNextProcTime();

      // ts > 0, 说明有数据需要发送
      if (ts > 0)
      {
         // wait until next processing time of the first socket on the list
         // 休眠至调度时间
         uint64_t currtime;
         CTimer::rdtsc(currtime);
         if (currtime < ts)
            self->m_pTimer->sleepto(ts);  // 休眠

         // 发送数据
         // it is time to send the next pkt
         sockaddr* addr;
         CPacket pkt;
         // 从发送列表中pop数据
         if (self->m_pSndUList->pop(addr, pkt) < 0)
            continue;

         // 通过UDP通道发送数据
         self->m_pChannel->sendto(addr, pkt);
      }
      // 没有数据需要发送，休眠
      else
      {
         // wait here if there is no sockets with data to be sent
         // 等待条件变量m_WindowCond
         #ifndef WIN32
            pthread_mutex_lock(&self->m_WindowLock);
            if (!self->m_bClosing && (self->m_pSndUList->m_iLastEntry < 0))
               pthread_cond_wait(&self->m_WindowCond, &self->m_WindowLock);
            pthread_mutex_unlock(&self->m_WindowLock);
         #else
            WaitForSingleObject(self->m_WindowCond, INFINITE);
         #endif
      }
   }

   #ifndef WIN32
      return NULL;
   #else
      SetEvent(self->m_ExitCond);
      return 0;
   #endif
}

// 握手及控制报文需要通过UDP立即发送到对端，不经过发送缓冲区
int CSndQueue::sendto(const sockaddr* addr, CPacket& packet)
{
   // send out the packet immediately (high priority), this is a control packet
   m_pChannel->sendto(addr, packet);
   return packet.getLength();
}


//
CRcvUList::CRcvUList():
m_pUList(NULL),
m_pLast(NULL)
{
}

CRcvUList::~CRcvUList()
{
}

void CRcvUList::insert(const CUDT* u)
{
   CRNode* n = u->m_pRNode;
   CTimer::rdtsc(n->m_llTimeStamp);

   if (NULL == m_pUList)
   {
      // empty list, insert as the single node
      n->m_pPrev = n->m_pNext = NULL;
      m_pLast = m_pUList = n;

      return;
   }

   // always insert at the end for RcvUList
   n->m_pPrev = m_pLast;
   n->m_pNext = NULL;
   m_pLast->m_pNext = n;
   m_pLast = n;
}

void CRcvUList::remove(const CUDT* u)
{
   CRNode* n = u->m_pRNode;

   if (!n->m_bOnList)
      return;

   if (NULL == n->m_pPrev)
   {
      // n is the first node
      m_pUList = n->m_pNext;
      if (NULL == m_pUList)
         m_pLast = NULL;
      else
         m_pUList->m_pPrev = NULL;
   }
   else
   {
      n->m_pPrev->m_pNext = n->m_pNext;
      if (NULL == n->m_pNext)
      {
         // n is the last node
         m_pLast = n->m_pPrev;
      }
      else
         n->m_pNext->m_pPrev = n->m_pPrev;
   }

   n->m_pNext = n->m_pPrev = NULL;
}

// 更新一个UDT实例，将其调整到链表的末尾
void CRcvUList::update(const CUDT* u)
{
   CRNode* n = u->m_pRNode;

   if (!n->m_bOnList)
      return;

   CTimer::rdtsc(n->m_llTimeStamp);

   // if n is the last node, do not need to change
   // 已经是最后一个节点了，不必进行调整
   if (NULL == n->m_pNext)
      return;

   // 当前节点是第一个节点的情况
   if (NULL == n->m_pPrev)
   {
      m_pUList = n->m_pNext;
      m_pUList->m_pPrev = NULL;
   }
   // 当前节点是中间节点的情况
   else
   {
      n->m_pPrev->m_pNext = n->m_pNext;
      n->m_pNext->m_pPrev = n->m_pPrev;
   }

   n->m_pPrev = m_pLast;
   n->m_pNext = NULL;
   m_pLast->m_pNext = n;
   m_pLast = n;
}

//
CHash::CHash():
m_pBucket(NULL),
m_iHashSize(0)
{
}

CHash::~CHash()
{
   for (int i = 0; i < m_iHashSize; ++ i)
   {
      CBucket* b = m_pBucket[i];
      while (NULL != b)
      {
         CBucket* n = b->m_pNext;
         delete b;
         b = n;
      }
   }

   delete [] m_pBucket;
}

void CHash::init(int size)
{
   m_pBucket = new CBucket* [size];

   for (int i = 0; i < size; ++ i)
      m_pBucket[i] = NULL;

   m_iHashSize = size;
}

CUDT* CHash::lookup(int32_t id)
{
   // simple hash function (% hash table size); suitable for socket descriptors
   CBucket* b = m_pBucket[id % m_iHashSize];

   while (NULL != b)
   {
      if (id == b->m_iID)
         return b->m_pUDT;
      b = b->m_pNext;
   }

   return NULL;
}

void CHash::insert(int32_t id, CUDT* u)
{
   CBucket* b = m_pBucket[id % m_iHashSize];

   CBucket* n = new CBucket;
   n->m_iID = id;
   n->m_pUDT = u;
   n->m_pNext = b;

   m_pBucket[id % m_iHashSize] = n;
}

void CHash::remove(int32_t id)
{
   CBucket* b = m_pBucket[id % m_iHashSize];
   CBucket* p = NULL;

   while (NULL != b)
   {
      if (id == b->m_iID)
      {
         if (NULL == p)
            m_pBucket[id % m_iHashSize] = b->m_pNext;
         else
            p->m_pNext = b->m_pNext;

         delete b;

         return;
      }

      p = b;
      b = b->m_pNext;
   }
}


// 会合模式
CRendezvousQueue::CRendezvousQueue():
m_lRendezvousID(),
m_RIDVectorLock()
{
   #ifndef WIN32
      pthread_mutex_init(&m_RIDVectorLock, NULL);
   #else
      m_RIDVectorLock = CreateMutex(NULL, false, NULL);
   #endif
}

CRendezvousQueue::~CRendezvousQueue()
{
   #ifndef WIN32
      pthread_mutex_destroy(&m_RIDVectorLock);
   #else
      CloseHandle(m_RIDVectorLock);
   #endif

   for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++ i)
   {
      if (AF_INET == i->m_iIPversion)
         delete (sockaddr_in*)i->m_pPeerAddr;
      else
         delete (sockaddr_in6*)i->m_pPeerAddr;
   }

   m_lRendezvousID.clear();
}

void CRendezvousQueue::insert(const UDTSOCKET& id, CUDT* u, int ipv, const sockaddr* addr, uint64_t ttl)
{
   // lock_gaurd
   CGuard vg(m_RIDVectorLock);

   // 向list中插入一个UDT实例
   CRL r;
   r.m_iID = id;
   r.m_pUDT = u;
   r.m_iIPversion = ipv;
   r.m_pPeerAddr = (AF_INET == ipv) ? (sockaddr*)new sockaddr_in : (sockaddr*)new sockaddr_in6;
   memcpy(r.m_pPeerAddr, addr, (AF_INET == ipv) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
   r.m_ullTTL = ttl;

   m_lRendezvousID.push_back(r);
}

void CRendezvousQueue::remove(const UDTSOCKET& id)
{
   CGuard vg(m_RIDVectorLock);

   for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++ i)
   {
      if (i->m_iID == id)
      {
         if (AF_INET == i->m_iIPversion)
            delete (sockaddr_in*)i->m_pPeerAddr;
         else
            delete (sockaddr_in6*)i->m_pPeerAddr;

         m_lRendezvousID.erase(i);

         return;
      }
   }
}

CUDT* CRendezvousQueue::retrieve(const sockaddr* addr, UDTSOCKET& id)
{
   CGuard vg(m_RIDVectorLock);

   // TODO: optimize search
   for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++ i)
   {
      if (CIPAddress::ipcmp(addr, i->m_pPeerAddr, i->m_iIPversion) && ((0 == id) || (id == i->m_iID)))
      {
         id = i->m_iID;
         return i->m_pUDT;
      }
   }

   return NULL;
}

void CRendezvousQueue::updateConnStatus()
{
   if (m_lRendezvousID.empty())
      return;

   CGuard vg(m_RIDVectorLock);

   for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++ i)
   {
      // avoid sending too many requests, at most 1 request per 250ms
      // 避免发送太多连接请求，每250ms最多发送1个请求
      if (CTimer::getTime() - i->m_pUDT->m_llLastReqTime > 250000)
      {
         // 已达到ttl时间
         if (CTimer::getTime() >= i->m_ullTTL)
         {
            // connection timer expired, acknowledge app via epoll
            // 更新连接状态，以及epoll中该套接字的状态
            i->m_pUDT->m_bConnecting = false;
            CUDT::s_UDTUnited.m_EPoll.update_events(i->m_iID, i->m_pUDT->m_sPollID, UDT_EPOLL_ERR, true);
            continue;
         }

         // 创建连接请求数据包
         CPacket request;
         char* reqdata = new char [i->m_pUDT->m_iPayloadSize];
         // 握手报文封包
         request.pack(0, NULL, reqdata, i->m_pUDT->m_iPayloadSize);
         // ID = 0, connection request
         request.m_iID = !i->m_pUDT->m_bRendezvous ? 0 : i->m_pUDT->m_ConnRes.m_iID;
         int hs_size = i->m_pUDT->m_iPayloadSize;
         // 握手报文序列化，即按固定格式封包
         i->m_pUDT->m_ConnReq.serialize(reqdata, hs_size);
         request.setLength(hs_size);
         // 握手报文需要通过UDP立即发送到对端,不经过发送缓冲区
         i->m_pUDT->m_pSndQueue->sendto(i->m_pPeerAddr, request);
         i->m_pUDT->m_llLastReqTime = CTimer::getTime();
         delete [] reqdata;
      }
   }
}

CRcvQueue::CRcvQueue():
m_WorkerThread(),
m_UnitQueue(),
m_pRcvUList(NULL),
m_pHash(NULL),
m_pChannel(NULL),
m_pTimer(NULL),
m_iPayloadSize(),
m_bClosing(false),
m_ExitCond(),
m_LSLock(),
m_pListener(NULL),
m_pRendezvousQueue(NULL),
m_vNewEntry(),
m_IDLock(),
m_mBuffer(),
m_PassLock(),
m_PassCond()
{
   #ifndef WIN32
      pthread_mutex_init(&m_PassLock, NULL);
      pthread_cond_init(&m_PassCond, NULL);
      pthread_mutex_init(&m_LSLock, NULL);
      pthread_mutex_init(&m_IDLock, NULL);
   #else
      m_PassLock = CreateMutex(NULL, false, NULL);
      m_PassCond = CreateEvent(NULL, false, false, NULL);
      m_LSLock = CreateMutex(NULL, false, NULL);
      m_IDLock = CreateMutex(NULL, false, NULL);
      m_ExitCond = CreateEvent(NULL, false, false, NULL);
   #endif
}

CRcvQueue::~CRcvQueue()
{
   m_bClosing = true;

   #ifndef WIN32
      if (0 != m_WorkerThread)
         pthread_join(m_WorkerThread, NULL);
      pthread_mutex_destroy(&m_PassLock);
      pthread_cond_destroy(&m_PassCond);
      pthread_mutex_destroy(&m_LSLock);
      pthread_mutex_destroy(&m_IDLock);
   #else
      if (NULL != m_WorkerThread)
         WaitForSingleObject(m_ExitCond, INFINITE);
      CloseHandle(m_WorkerThread);
      CloseHandle(m_PassLock);
      CloseHandle(m_PassCond);
      CloseHandle(m_LSLock);
      CloseHandle(m_IDLock);
      CloseHandle(m_ExitCond);
   #endif

   delete m_pRcvUList;
   delete m_pHash;
   delete m_pRendezvousQueue;

   // remove all queued messages
   for (map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.begin(); i != m_mBuffer.end(); ++ i)
   {
      while (!i->second.empty())
      {
         CPacket* pkt = i->second.front();
         delete [] pkt->m_pcData;
         delete pkt;
         i->second.pop();
      }
   }
}

/*
   接收队列初始化
   1. 初始化数据单元队列
   2. 初始化哈希表
   3. 关联UDP通道
   4. 关联定时器
   5. 初始化UDT接收实例列表
   6. 初始化会合连接模式
   7. 创建接收数据的工作线程
 */
void CRcvQueue::init(int qsize, int payload, int version, int hsize, CChannel* cc, CTimer* t)
{
   // 数据包负载大小
   m_iPayloadSize = payload;

   // 数据单元队列初始化
   m_UnitQueue.init(qsize, payload, version);

   // 哈希表初始化
   m_pHash = new CHash;
   m_pHash->init(hsize);

   // 关联UDP通道
   m_pChannel = cc;
   // 关联定时器
   m_pTimer = t;
   // UDT接收实例列表初始化
   m_pRcvUList = new CRcvUList;
   // 会合连接模式
   m_pRendezvousQueue = new CRendezvousQueue;

   #ifndef WIN32
      // 接收数据的工作线程
      if (0 != pthread_create(&m_WorkerThread, NULL, CRcvQueue::worker, this))
      {
         m_WorkerThread = 0;
         throw CUDTException(3, 1);
      }
   #else
      DWORD threadID;
      m_WorkerThread = CreateThread(NULL, 0, CRcvQueue::worker, this, 0, &threadID);
      if (NULL == m_WorkerThread)
         throw CUDTException(3, 1);
   #endif
}

// UDT接收队列工作线程，处理新的连接和新的数据包
/*
   1. 首先检查是否有新的连接，如果有，则将新的连接插入到UDT接收实例列表和哈希表中
   2. 接受新的包，判断是数据包还是控制包，按相应类型进行处理
   3. m_pUList中存储着新接受到的数据包，需要更新m_pUList列表的状态
   4. 释放资源
*/
#ifndef WIN32
   void* CRcvQueue::worker(void* param)
#else
   DWORD WINAPI CRcvQueue::worker(LPVOID param)
#endif
{
   CRcvQueue* self = (CRcvQueue*)param;

   // IPv4/IPv6
   sockaddr* addr = (AF_INET == self->m_UnitQueue.m_iIPversion) ? (sockaddr*) new sockaddr_in : (sockaddr*) new sockaddr_in6;
   CUDT* u = NULL;
   int32_t id;

   while (!self->m_bClosing)
   {
      // 禁止忙等，通过m_pTimer->tick()让出CPU时间片
      #ifdef NO_BUSY_WAITING
         self->m_pTimer->tick();
      #endif

      // check waiting list, if new socket, insert it to the list
      // 检查是否有新的连接待处理
      while (self->ifNewEntry())
      {
         // 获取第一个待处理的连接
         CUDT* ne = self->getNewEntry();
         if (NULL != ne)
         {
            // 插入到UDT接收实例列表
            self->m_pRcvUList->insert(ne);
            // 插入到哈希表
            self->m_pHash->insert(ne->m_SocketID, ne);
         }
      }

      // find next available slot for incoming packet
      // 从m_UnitQueue中获取一个空闲的数据单元
      CUnit* unit = self->m_UnitQueue.getNextAvailUnit();
      // 接收缓冲区已满，则跳过这个数据包
      if (NULL == unit)
      {
         // no space, skip this packet
         CPacket temp;
         temp.m_pcData = new char[self->m_iPayloadSize];
         temp.setLength(self->m_iPayloadSize);
         self->m_pChannel->recvfrom(addr, temp);
         delete [] temp.m_pcData;
         goto TIMER_CHECK;
      }

      // 到这里，说明发送缓冲区还有足够的空间，可以接收数据包

      unit->m_Packet.setLength(self->m_iPayloadSize);

      // reading next incoming packet, recvfrom returns -1 is nothing has been received
      // 接收出错
      if (self->m_pChannel->recvfrom(addr, unit->m_Packet) < 0)
         goto TIMER_CHECK;

      // 获取数据包的id
      id = unit->m_Packet.m_iID;

      // ID 0 is for connection request, which should be passed to the listening socket or rendezvous sockets
      // 0 == id，说明这是一个连接请求包
      if (0 == id)
      {
         // 监听模式，调用listen，创建新的UDT连接
         if (NULL != self->m_pListener){
            std::cout << __func__ << " : " << __LINE__ << std::endl;
            self->m_pListener->listen(addr, unit->m_Packet);
         }
         // 会合连接模式
         else if (NULL != (u = self->m_pRendezvousQueue->retrieve(addr, id)))
         {
            // asynchronous connect: call connect here
            // otherwise wait for the UDT socket to retrieve this packet
            if (!u->m_bSynRecving){
               std::cout << __func__ << " : " << __LINE__ << std::endl;
               u->connect(unit->m_Packet);
            }
            else{
               std::cout << __func__ << " : " << __LINE__ << std::endl;
               self->storePkt(id, unit->m_Packet.clone());
            }
         }
      }
      // id > 0, 说明这是一个数据包
      else if (id > 0)
      {
         // 根据哈希表查找UDT实例
         if (NULL != (u = self->m_pHash->lookup(id)))
         {
            // 检查对端地址是否匹配
            if (CIPAddress::ipcmp(addr, u->m_pPeerAddr, u->m_iIPversion))
            {
               // 检查连接状态，连接正常则开始处理数据包
               if (u->m_bConnected && !u->m_bBroken && !u->m_bClosing)
               {
                  // 包类型：0表示数据包，1表示控制包
                  if (0 == unit->m_Packet.getFlag()){
                     u->processData(unit);
                  }
                  // 控制包
                  else{
                     u->processCtrl(unit->m_Packet);
                  }

                  u->checkTimers();
                  // 更新m_pRcvUList
                  self->m_pRcvUList->update(u);
               }
            }
         }
         // 会合连接模式
         else if (NULL != (u = self->m_pRendezvousQueue->retrieve(addr, id)))
         {
            if (!u->m_bSynRecving)
               u->connect(unit->m_Packet);
            else
               self->storePkt(id, unit->m_Packet.clone());
         }
      }

TIMER_CHECK:
      // take care of the timing event for all UDT sockets

      // 当前时间戳
      uint64_t currtime;
      CTimer::rdtsc(currtime);

      CRNode* ul = self->m_pRcvUList->m_pUList;
      // 100ms的时间窗口
      uint64_t ctime = currtime - 100000 * CTimer::getCPUFrequency();
      // ul->m_llTimeStamp < ctime 说明这个UDT实例已经超过100ms没有更新了，需要进行检查
      while ((NULL != ul) && (ul->m_llTimeStamp < ctime))
      {
         CUDT* u = ul->m_pUDT;

         /*
            检查连接状态
            1. 连接正常，调用update()将当前节点移动到列表m_pRcvUList末尾
            2. 连接异常，调用remove()从哈希表和m_pRcvUList列表中移除
            3. 由于重新调整了m_pRcvUList，故需要重新获取m_pRcvUList的头节点
         */

         // 连接正常
         if (u->m_bConnected && !u->m_bBroken && !u->m_bClosing)
         {
            u->checkTimers();
            self->m_pRcvUList->update(u);
         }
         // 连接异常，则从哈希表和UDT接收实例列表中移除
         else
         {
            // the socket must be removed from Hash table first, then RcvUList
            self->m_pHash->remove(u->m_SocketID);
            self->m_pRcvUList->remove(u);
            u->m_pRNode->m_bOnList = false;
         }

         // 由于重新调整了m_pRcvUList，故需要重新获取m_pRcvUList的头节点
         ul = self->m_pRcvUList->m_pUList;
      }

      // Check connection requests status for all sockets in the RendezvousQueue.
      // 更新会合模式下的连接状态
      self->m_pRendezvousQueue->updateConnStatus();
   }

   // 释放资源
   if (AF_INET == self->m_UnitQueue.m_iIPversion)
      delete (sockaddr_in*)addr;
   else
      delete (sockaddr_in6*)addr;

   #ifndef WIN32
      return NULL;
   #else
      SetEvent(self->m_ExitCond);
      return 0;
   #endif
}

/*
   从接收缓冲区中读取指定UDT套接字的packet
   1. 先根据UDTSOCKET找到对应的packet队列
   2. 从packet队列中获取最早的packet
   3. 释放空间，返回数据包长度
*/
int CRcvQueue::recvfrom(int32_t id, CPacket& packet)
{
   CGuard bufferlock(m_PassLock);

   // 从缓冲区中查找指定ID的packet队列
   map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);

   // 没有找到packet,在条件变量m_PassCond上休眠等待，最多等待大约1秒
   if (i == m_mBuffer.end())
   {
      // 最多等待大约1秒
      #ifndef WIN32
         uint64_t now = CTimer::getTime();
         timespec timeout;

         timeout.tv_sec = now / 1000000 + 1;
         timeout.tv_nsec = (now % 1000000) * 1000;

         // 在条件变量m_PassCond上休眠等待，最多等待大约1秒
         pthread_cond_timedwait(&m_PassCond, &m_PassLock, &timeout);
      #else
         ReleaseMutex(m_PassLock);
         WaitForSingleObject(m_PassCond, 1000);
         WaitForSingleObject(m_PassLock, INFINITE);
      #endif

      // 再次检查缓冲区中是否存在指定ID的packet
      i = m_mBuffer.find(id);
      // 不存在，返回-1
      if (i == m_mBuffer.end())
      {
         packet.setLength(-1);
         return -1;
      }
   }

   // 到这一步，说明找到了对应的packet队列

   // retrieve the earliest packet
   // 获取最早的packet
   CPacket* newpkt = i->second.front();

   // 检查数据包长度，确保packet的容量足够大
   if (packet.getLength() < newpkt->getLength())
   {
      packet.setLength(-1);
      return -1;
   }

   // copy packet content
   // 拷贝数据包
   memcpy(packet.m_nHeader, newpkt->m_nHeader, CPacket::m_iPktHdrSize);
   memcpy(packet.m_pcData, newpkt->m_pcData, newpkt->getLength());
   packet.setLength(newpkt->getLength());

   // 释放空间
   delete [] newpkt->m_pcData;
   delete newpkt;

   // remove this message from queue, 
   // if no more messages left for this socket, release its data structure
   // 从packet队列中删除这个packet
   i->second.pop();
   // 如果这个packet队列中没有更多的packet，则删除这个packet队列
   if (i->second.empty())
      m_mBuffer.erase(i);

   // 返回数据包长度
   return packet.getLength();
}

int CRcvQueue::setListener(CUDT* u)
{
   std::cout << "CRecvQueue::setListener()..." << std::endl;
   CGuard lslock(m_LSLock);

   if (NULL != m_pListener)
      return -1;

   m_pListener = u;
   return 0;
}

void CRcvQueue::removeListener(const CUDT* u)
{
   CGuard lslock(m_LSLock);

   if (u == m_pListener)
      m_pListener = NULL;
}

void CRcvQueue::registerConnector(const UDTSOCKET& id, CUDT* u, int ipv, const sockaddr* addr, uint64_t ttl)
{
   m_pRendezvousQueue->insert(id, u, ipv, addr, ttl);
}

void CRcvQueue::removeConnector(const UDTSOCKET& id)
{
   m_pRendezvousQueue->remove(id);

   CGuard bufferlock(m_PassLock);

   map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);
   if (i != m_mBuffer.end())
   {
      while (!i->second.empty())
      {
         delete [] i->second.front()->m_pcData;
         delete i->second.front();
         i->second.pop();
      }
      m_mBuffer.erase(i);
   }
}

void CRcvQueue::setNewEntry(CUDT* u)
{
   CGuard listguard(m_IDLock);
   m_vNewEntry.push_back(u);
}

// 检查是否有新的连接待处理
bool CRcvQueue::ifNewEntry()
{
   return !(m_vNewEntry.empty());
}

// 获取第一个待处理的连接
CUDT* CRcvQueue::getNewEntry()
{
   CGuard listguard(m_IDLock);

   // 没有新的连接，返回NULL
   if (m_vNewEntry.empty())
      return NULL;

   // 获取第一个待处理的连接
   CUDT* u = (CUDT*)*(m_vNewEntry.begin());
   m_vNewEntry.erase(m_vNewEntry.begin());

   return u;
}

// 保存握手阶段的控制报文
void CRcvQueue::storePkt(int32_t id, CPacket* pkt)
{
   // lock_guard
   CGuard bufferlock(m_PassLock);   

   // 查找指定的UDTSOCKET
   map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);

   // UDTSOCKET不存在，则创建一个新的队列
   if (i == m_mBuffer.end())
   {
      m_mBuffer[id].push(pkt);

      // 唤醒CRcvQueue::recvfrom，从接收缓冲区中取数据
      #ifndef WIN32
         pthread_cond_signal(&m_PassCond);
      #else
         SetEvent(m_PassCond);
      #endif
   }
   // UDTSOCKET存在，向队尾添加packet
   else
   {
      //avoid storing too many packets, in case of malfunction or attack
      // 避免存储太多的packet，最多存储16个packets
      // 队列m_mBuffer存储的是握手阶段的控制包，而不是数据包
      // 限制为16可以避免对端短时间内快速创建多个连接，防止握手阶段的Dos攻击
      if (i->second.size() > 16)
         return;

      i->second.push(pkt);
   }
}
