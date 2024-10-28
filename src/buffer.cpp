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
   Yunhong Gu, last updated 03/12/2011
*****************************************************************************/

#include <cstring>
#include <cmath>
#include "buffer.h"

using namespace std;

CSndBuffer::CSndBuffer(int size, int mss):
m_BufLock(),
m_pBlock(NULL),
m_pFirstBlock(NULL),
m_pCurrBlock(NULL),
m_pLastBlock(NULL),
m_pBuffer(NULL),
m_iNextMsgNo(1),
m_iSize(size),
m_iMSS(mss),
m_iCount(0)
{
   // initial physical buffer of "size"
   // 堆内存
   m_pBuffer = new Buffer;
   // 实际分配的内存 = 数据块个数m_iSize * 最大包大小m_iMSS
   m_pBuffer->m_pcData = new char [m_iSize * m_iMSS];
   // 数据块个数
   m_pBuffer->m_iSize = m_iSize;
   m_pBuffer->m_pNext = NULL;

   // circular linked list for out bound packets
   // 循环链表，链表中的每个节点都是一个数据块
   m_pBlock = new Block;
   Block* pb = m_pBlock;
   for (int i = 1; i < m_iSize; ++ i)
   {
      pb->m_pNext = new Block;
      pb->m_iMsgNo = 0;
      pb = pb->m_pNext;
   }
   pb->m_pNext = m_pBlock;

   pb = m_pBlock;

   // 循环链表中的指针，指向真实的堆内存
   char* pc = m_pBuffer->m_pcData;
   for (int i = 0; i < m_iSize; ++ i)
   {
      pb->m_pcData = pc;
      pb = pb->m_pNext;
      pc += m_iMSS;     // 指针偏移到下一个数据块
   }

   // 初始化数据块指针都指向申请的堆内存起始位置
   m_pFirstBlock = m_pCurrBlock = m_pLastBlock = m_pBlock;

   // 初始化锁
   #ifndef WIN32
      pthread_mutex_init(&m_BufLock, NULL);
   #else
      m_BufLock = CreateMutex(NULL, false, NULL);
   #endif
}

CSndBuffer::~CSndBuffer()
{
   // 释放堆内存
   Block* pb = m_pBlock->m_pNext;
   while (pb != m_pBlock)
   {
      Block* temp = pb;
      pb = pb->m_pNext;
      delete temp;
   }
   delete m_pBlock;

   while (m_pBuffer != NULL)
   {
      Buffer* temp = m_pBuffer;
      m_pBuffer = m_pBuffer->m_pNext;
      delete [] temp->m_pcData;
      delete temp;
   }

   // 销毁锁
   #ifndef WIN32
      pthread_mutex_destroy(&m_BufLock);
   #else
      CloseHandle(m_BufLock);
   #endif
}

void CSndBuffer::addBuffer(const char* data, int len, int ttl, bool order)
{
   // 计算要插入的数据需要占用多少数据块
   int size = len / m_iMSS;
   if ((len % m_iMSS) != 0)
      size ++;

   // dynamically increase sender buffer
   // 动态增大发送缓冲区
   while (size + m_iCount >= m_iSize)
      increase();

   uint64_t time = CTimer::getTime();
   // 是否需要按序发送
   int32_t inorder = order;
   inorder <<= 29;

   // 指向最后一个数据块
   Block* s = m_pLastBlock;
   // 将数据插入到数据块中
   for (int i = 0; i < size; ++ i)
   {
      // 待插入的数据长度
      int pktlen = len - i * m_iMSS;
      if (pktlen > m_iMSS)
         pktlen = m_iMSS;
      // 将数据拷贝到数据块中
      memcpy(s->m_pcData, data + i * m_iMSS, pktlen);
      // 数据块中有效数据的大小，有些数据可能并不会占用一个完整的数据块
      s->m_iLength = pktlen;

      // m_iMsgNo的bit[29]表示是否需要按序发送, m_iNextMsgNo在构造是被初始化为0
      s->m_iMsgNo = m_iNextMsgNo | inorder;
      
      // 下面这个逻辑用来进行data重组
      // data起始：data占用的首个数据块, m_iMsgNo的bit[31]为1
      if (i == 0)
         s->m_iMsgNo |= 0x80000000;
      // data结束：data占用的最后一个数据块,m_iMsgNo的bit[30]为1
      if (i == size - 1)
         s->m_iMsgNo |= 0x40000000;

      // 数据被放入发送缓冲区时的时间戳
      s->m_OriginTime = time;
      s->m_iTTL = ttl;

      // 下一个数据块
      s = s->m_pNext;
   }
   // 更新最后一个数据块指针
   m_pLastBlock = s;

   // 更新发送缓冲区中已被占用的数据块数量
   CGuard::enterCS(m_BufLock);
   m_iCount += size;
   CGuard::leaveCS(m_BufLock);

   // 更新消息编号，超出后回绕至1
   m_iNextMsgNo ++;
   if (m_iNextMsgNo == CMsgNo::m_iMaxMsgNo)
      m_iNextMsgNo = 1;
}

int CSndBuffer::addBufferFromFile(fstream& ifs, int len)
{
   int size = len / m_iMSS;
   if ((len % m_iMSS) != 0)
      size ++;

   // dynamically increase sender buffer
   while (size + m_iCount >= m_iSize)
      increase();

   Block* s = m_pLastBlock;
   int total = 0;
   for (int i = 0; i < size; ++ i)
   {
      if (ifs.bad() || ifs.fail() || ifs.eof())
         break;

      int pktlen = len - i * m_iMSS;
      if (pktlen > m_iMSS)
         pktlen = m_iMSS;

      ifs.read(s->m_pcData, pktlen);
      if ((pktlen = ifs.gcount()) <= 0)
         break;

      // currently file transfer is only available in streaming mode, message is always in order, ttl = infinite
      s->m_iMsgNo = m_iNextMsgNo | 0x20000000;
      if (i == 0)
         s->m_iMsgNo |= 0x80000000;
      if (i == size - 1)
         s->m_iMsgNo |= 0x40000000;

      s->m_iLength = pktlen;
      s->m_iTTL = -1;
      s = s->m_pNext;

      total += pktlen;
   }
   m_pLastBlock = s;

   CGuard::enterCS(m_BufLock);
   m_iCount += size;
   CGuard::leaveCS(m_BufLock);

   m_iNextMsgNo ++;
   if (m_iNextMsgNo == CMsgNo::m_iMaxMsgNo)
      m_iNextMsgNo = 1;

   return total;
}

int CSndBuffer::readData(char** data, int32_t& msgno)
{
   // No data to read
   if (m_pCurrBlock == m_pLastBlock)
      return 0;

   *data = m_pCurrBlock->m_pcData;
   int readlen = m_pCurrBlock->m_iLength;
   msgno = m_pCurrBlock->m_iMsgNo;

   m_pCurrBlock = m_pCurrBlock->m_pNext;

   return readlen;
}

// 按偏移量从发送缓冲区中读数据，并且丢弃超时未发送地数据
int CSndBuffer::readData(char** data, const int offset, int32_t& msgno, int& msglen)
{
   CGuard bufferguard(m_BufLock);

   Block* p = m_pFirstBlock;

   // 偏移，按数据块进行偏移
   for (int i = 0; i < offset; ++ i)
      p = p->m_pNext;

   // 数据是否过期，如果当前时间-数据产生的时间大于数据的TTL，则认为数据过期；
   // 删除数据并返回-1，表示读取失败
   if ((p->m_iTTL >= 0) && ((CTimer::getTime() - p->m_OriginTime) / 1000 > (uint64_t)p->m_iTTL))
   {
      // 获取消息号
      msgno = p->m_iMsgNo & 0x1FFFFFFF;

      msglen = 1;
      p = p->m_pNext;
      bool move = false;
      // 移除所有消息号相同的数据块
      while (msgno == (p->m_iMsgNo & 0x1FFFFFFF))
      {
         if (p == m_pCurrBlock)
            move = true;
         p = p->m_pNext;
         if (move)
            m_pCurrBlock = p;
         msglen ++;
      }

      return -1;
   }

   // 正常读取数据，读一个数据块
   *data = p->m_pcData;
   int readlen = p->m_iLength;
   msgno = p->m_iMsgNo;

   return readlen;
}

void CSndBuffer::ackData(int offset)
{
   CGuard bufferguard(m_BufLock);

   // 丢弃已经被对端确认的数据
   for (int i = 0; i < offset; ++ i)
      m_pFirstBlock = m_pFirstBlock->m_pNext;

   // 更新发送缓冲区中已占用的数据块数量
   m_iCount -= offset;

   CTimer::triggerEvent();
}

int CSndBuffer::getCurrBufSize() const
{
   return m_iCount;
}

// 动态增大发送缓冲区
void CSndBuffer::increase()
{
   int unitsize = m_pBuffer->m_iSize;

   // new physical buffer
   // 申请堆空间
   Buffer* nbuf = NULL;
   try
   {
      nbuf  = new Buffer;
      nbuf->m_pcData = new char [unitsize * m_iMSS];
   }
   catch (...)
   {
      delete nbuf;
      throw CUDTException(3, 2, 0);
   }
   nbuf->m_iSize = unitsize;
   nbuf->m_pNext = NULL;

   // insert the buffer at the end of the buffer list
   Buffer* p = m_pBuffer;
   while (NULL != p->m_pNext)
      p = p->m_pNext;
   p->m_pNext = nbuf;

   // new packet blocks
   Block* nblk = NULL;
   try
   {
      nblk = new Block;
   }
   catch (...)
   {
      delete nblk;
      throw CUDTException(3, 2, 0);
   }
   Block* pb = nblk;
   for (int i = 1; i < unitsize; ++ i)
   {
      pb->m_pNext = new Block;
      pb = pb->m_pNext;
   }

   // insert the new blocks onto the existing one
   pb->m_pNext = m_pLastBlock->m_pNext;
   m_pLastBlock->m_pNext = nblk;

   pb = nblk;
   char* pc = nbuf->m_pcData;
   for (int i = 0; i < unitsize; ++ i)
   {
      pb->m_pcData = pc;
      pb = pb->m_pNext;
      pc += m_iMSS;
   }

   m_iSize += unitsize;
}

////////////////////////////////////////////////////////////////////////////////

CRcvBuffer::CRcvBuffer(CUnitQueue* queue, int bufsize):
m_pUnit(NULL),
m_iSize(bufsize),
m_pUnitQueue(queue),
m_iStartPos(0),
m_iLastAckPos(0),
m_iMaxPos(0),
m_iNotch(0)
{
   // 数据单元指针数组
   m_pUnit = new CUnit* [m_iSize];
   // 数据单元指针数组初始化
   for (int i = 0; i < m_iSize; ++ i)
      m_pUnit[i] = NULL;
}

CRcvBuffer::~CRcvBuffer()
{
   for (int i = 0; i < m_iSize; ++ i)
   {
      if (NULL != m_pUnit[i])
      {
         m_pUnit[i]->m_iFlag = 0;
         -- m_pUnitQueue->m_iCount;
      }
   }

   delete [] m_pUnit;
}

int CRcvBuffer::addData(CUnit* unit, int offset)
{
   // 新增数据的位置
   int pos = (m_iLastAckPos + offset) % m_iSize;
   // 更新最远数据的位置
   if (offset > m_iMaxPos)
      m_iMaxPos = offset;

   // 数据单元已被占用
   if (NULL != m_pUnit[pos])
      return -1;
   
   // 存储数据
   m_pUnit[pos] = unit;

   // 更新数据单元状态为被占用状态
   unit->m_iFlag = 1;
   // 被占用的数据单元统计
   ++ m_pUnitQueue->m_iCount;

   return 0;
}

int CRcvBuffer::readBuffer(char* data, int len)
{
   // 接收缓冲区读起始位置
   int p = m_iStartPos;
   // 接收缓冲区中最新一个已确认的数据单元位置，即读结束位置
   int lastack = m_iLastAckPos;
   // 剩余需要读取的字节数
   int rs = len;

   while ((p != lastack) && (rs > 0))
   {
      // 数据单元中包的长度
      // 在处理当前单元时，m_iNotch 用于记录已经读取了多少数据。这样，如果一次读取没有读取完整个单元的数据，下次可以从这个偏移量继续
      int unitsize = m_pUnit[p]->m_Packet.getLength() - m_iNotch;
      // 不要超出用户buffer的长度
      if (unitsize > rs)
         unitsize = rs;

      // 拷贝数据
      memcpy(data, m_pUnit[p]->m_Packet.m_pcData + m_iNotch, unitsize);
      // 数据偏移量
      data += unitsize;

      // 数据单元中的数据已经被拷贝完毕,释放数据单元
      if ((rs > unitsize) || (rs == m_pUnit[p]->m_Packet.getLength() - m_iNotch))
      {
         // 释放数据单元
         CUnit* tmp = m_pUnit[p];
         m_pUnit[p] = NULL;
         tmp->m_iFlag = 0;
         -- m_pUnitQueue->m_iCount;

         if (++ p == m_iSize)
            p = 0;
         
         // 读取了一个完整的数据单元，偏移量设置为0
         m_iNotch = 0;
      }
      else{
         // 一个数据单元没有被完全读取，记录偏移量，下次可以从这个偏移量继续
         m_iNotch += rs;
      }

      rs -= unitsize;
   }

   m_iStartPos = p;
   return len - rs;
}

int CRcvBuffer::readBufferToFile(fstream& ofs, int len)
{
   // 读数据起始位置
   int p = m_iStartPos;
   // 最后一条确认的数据单元位置
   int lastack = m_iLastAckPos;
   // 剩余需要读取的字节数
   int rs = len;

   while ((p != lastack) && (rs > 0))
   {
      // 待读取的数据长度
      int unitsize = m_pUnit[p]->m_Packet.getLength() - m_iNotch;
      // 更新需要读取的字节数
      if (unitsize > rs)
         unitsize = rs;

      // 写入文件
      ofs.write(m_pUnit[p]->m_Packet.m_pcData + m_iNotch, unitsize);
      if (ofs.fail())
         break;

      // 可以读取一个完整的数据单元
      if ((rs > unitsize) || (rs == m_pUnit[p]->m_Packet.getLength() - m_iNotch))
      {
         // 释放数据单元
         CUnit* tmp = m_pUnit[p];
         m_pUnit[p] = NULL;
         tmp->m_iFlag = 0;
         -- m_pUnitQueue->m_iCount;

         // 循环队列
         if (++ p == m_iSize)
            p = 0;

         // 取了一个完整的数据单元，偏移量设置为
         m_iNotch = 0;
      }
      // 没有读取一个完整的数据单元，记录偏移量，下次继续读取
      else{
         m_iNotch += rs;
      }

      // 剩余需要读取的字节数
      rs -= unitsize;
   }

   // 更新读起始位置
   m_iStartPos = p;

   // 返回成功读取的字节数
   return len - rs;
}

void CRcvBuffer::ackData(int len)
{
   // 更新已确认的数据单元位置
   m_iLastAckPos = (m_iLastAckPos + len) % m_iSize;
   // 最远数据位置
   m_iMaxPos -= len;
   // 循环
   if (m_iMaxPos < 0)
      m_iMaxPos = 0;

   // 唤醒条件变量
   CTimer::triggerEvent();
}

int CRcvBuffer::getAvailBufSize() const
{
   // One slot must be empty in order to tell the difference between "empty buffer" and "full buffer"
   return m_iSize - getRcvDataSize() - 1;
}

int CRcvBuffer::getRcvDataSize() const
{
   // 环形缓冲区数据量
   if (m_iLastAckPos >= m_iStartPos)
      return m_iLastAckPos - m_iStartPos;
   
   // 唤醒缓冲区数据量
   return m_iSize + m_iLastAckPos - m_iStartPos;
}

void CRcvBuffer::dropMsg(int32_t msgno)
{
   for (int i = m_iStartPos, n = (m_iLastAckPos + m_iMaxPos) % m_iSize; i != n; i = (i + 1) % m_iSize)
      if ((NULL != m_pUnit[i]) && (msgno == m_pUnit[i]->m_Packet.m_iMsgNo))
         m_pUnit[i]->m_iFlag = 3;
}

int CRcvBuffer::readMsg(char* data, int len)
{
   int p, q;
   bool passack;
   if (!scanMsg(p, q, passack))
      return 0;

   int rs = len;
   while (p != (q + 1) % m_iSize)
   {
      int unitsize = m_pUnit[p]->m_Packet.getLength();
      if ((rs >= 0) && (unitsize > rs))
         unitsize = rs;

      if (unitsize > 0)
      {
         memcpy(data, m_pUnit[p]->m_Packet.m_pcData, unitsize);
         data += unitsize;
         rs -= unitsize;
      }

      if (!passack)
      {
         CUnit* tmp = m_pUnit[p];
         m_pUnit[p] = NULL;
         tmp->m_iFlag = 0;
         -- m_pUnitQueue->m_iCount;
      }
      else
         m_pUnit[p]->m_iFlag = 2;

      if (++ p == m_iSize)
         p = 0;
   }

   if (!passack)
      m_iStartPos = (q + 1) % m_iSize;

   return len - rs;
}

int CRcvBuffer::getRcvMsgNum()
{
   int p, q;
   bool passack;
   return scanMsg(p, q, passack) ? 1 : 0;
}

bool CRcvBuffer::scanMsg(int& p, int& q, bool& passack)
{
   // empty buffer
   // 接收缓冲区为空
   if ((m_iStartPos == m_iLastAckPos) && (m_iMaxPos <= 0))
      return false;

   //skip all bad msgs at the beginning
   // 跳过缓冲区开头的坏数据单元
   while (m_iStartPos != m_iLastAckPos)
   {
      // 跳过空数据单元，直到找到一个有效的消息或到达缓冲区的最后位置
      if (NULL == m_pUnit[m_iStartPos])
      {
         if (++ m_iStartPos == m_iSize)
            m_iStartPos = 0;
         continue;
      }

      // 数据单元中有数据并且存在消息边界，则尝试获取一个完整的消息
      if ((1 == m_pUnit[m_iStartPos]->m_iFlag) && (m_pUnit[m_iStartPos]->m_Packet.getMsgBoundary() > 1))
      {
         // 用于判断消息的完整性
         bool good = true;

         // look ahead for the whole message
         // 从当前位置开始，向后查找消息边界，以获取一个完整的消息
         for (int i = m_iStartPos; i != m_iLastAckPos;)
         {
            // 获取UDP消息边界失败，说明不是一个完整的包
            if ((NULL == m_pUnit[i]) || (1 != m_pUnit[i]->m_iFlag))
            {
               // 消息不完整
               good = false;
               break;
            }

            // 找到了消息的边界，退出循环
            if ((m_pUnit[i]->m_Packet.getMsgBoundary() == 1) || (m_pUnit[i]->m_Packet.getMsgBoundary() == 3))
               break;

            // 更新循环索引，到达缓冲区末尾时回绕
            if (++ i == m_iSize)
               i = 0;
         }
         
         // 消息完整，退出循环
         if (good)
            break;
      }

      // 无论消息是否完整，都要释放数据单元；如果消息完整，向用户返回消息；如果消息不完整，直接丢弃
      CUnit* tmp = m_pUnit[m_iStartPos];
      m_pUnit[m_iStartPos] = NULL;
      tmp->m_iFlag = 0;
      -- m_pUnitQueue->m_iCount;

      // 更新起始指针，到达缓冲区末尾时回绕
      if (++ m_iStartPos == m_iSize)
         m_iStartPos = 0;
   }

   // 消息头索引
   p = -1;                  // message head
   // 消息尾索引
   q = m_iStartPos;         // message tail
   // passack表示是否越过确认点
   passack = m_iStartPos == m_iLastAckPos;
   // 是否找到完整消息
   bool found = false;

   // looking for the first message
   for (int i = 0, n = m_iMaxPos + getRcvDataSize(); i <= n; ++ i)
   {
      // 检查数据单元是否有效
      if ((NULL != m_pUnit[q]) && (1 == m_pUnit[q]->m_iFlag))
      {
         // 是否找到完整消息
         switch (m_pUnit[q]->m_Packet.getMsgBoundary())
         {
         case 3: // 11
            p = q;
            found = true;
            break;

         case 2: // 10
            p = q;
            break;

         case 1: // 01
            if (p != -1)
               found = true;
         }
      }
      else
      {
         // a hole in this message, not valid, restart search
         p = -1;
      }

      if (found)
      {
         // the msg has to be ack'ed or it is allowed to read out of order, and was not read before
         // 如果找到完整消息，检查是否已确认或允许乱序读取。如果是，则退出循环；否则，重置 found
         if (!passack || !m_pUnit[q]->m_Packet.getMsgOrderFlag())
            break;

         found = false;
      }

      // 更新环形缓冲区索引
      if (++ q == m_iSize)
         q = 0;

      // 找到了ack位置
      if (q == m_iLastAckPos)
         passack = true;
   }

   // no msg found
   // 如果没有找到消息，但消息大于缓冲区，返回部分消息
   if (!found)
   {
      // if the message is larger than the receiver buffer, return part of the message
      // 消息比缓冲区大，返回部分消息
      if ((p != -1) && ((q + 1) % m_iSize == p))
         found = true;
   }

   return found;
}
