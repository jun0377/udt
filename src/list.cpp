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

#include "list.h"

CSndLossList::CSndLossList(int size):
m_piData1(NULL),
m_piData2(NULL),
m_piNext(NULL),
m_iHead(-1),
m_iLength(0),
m_iSize(size),
m_iLastInsertPos(-1),
m_ListLock()
{
   m_piData1 = new int32_t [m_iSize];
   m_piData2 = new int32_t [m_iSize];
   m_piNext = new int [m_iSize];

   // -1 means there is no data in the node
   // 初始化数组，-1表示没有数据
   for (int i = 0; i < size; ++ i)
   {
      m_piData1[i] = -1;
      m_piData2[i] = -1;
   }

   // sender list needs mutex protection
   #ifndef WIN32
      pthread_mutex_init(&m_ListLock, 0);
   #else
      m_ListLock = CreateMutex(NULL, false, NULL);
   #endif
}

CSndLossList::~CSndLossList()
{
   delete [] m_piData1;
   delete [] m_piData2;
   delete [] m_piNext;

   #ifndef WIN32
      pthread_mutex_destroy(&m_ListLock);
   #else
      CloseHandle(m_ListLock);
   #endif
}

/*
   向重传列表中插入序列号
   1. 使用两个数组m_piData1和m_piData2分别保存起始序列号和终止序列号
   2. 使用m_piNext数组来记录下一个节点的位置
   3. 如果重传列表为空，则直接插入到head节点，即插入到头部位置
   4. 如果重传列表不为空，通过序列号偏移量寻找合适的插入位置
   5. 更新丢包计数m_iLength
   6. 返回丢包列表增加的长度
*/

int CSndLossList::insert(int32_t seqno1, int32_t seqno2)
{
   // lock_guard
   CGuard listguard(m_ListLock);

   // 0 == m_iLength 说明此时重传列表为空
   if (0 == m_iLength)
   {
      // insert data into an empty list

      // 向一个空的list中插入数据，插入到head节点
      m_iHead = 0;
      // 起始序列号
      m_piData1[m_iHead] = seqno1;
      // 终止序列号
      if (seqno2 != seqno1)
         m_piData2[m_iHead] = seqno2;

      // 下一个节点的位置为-1, 表示没有下一个节点
      m_piNext[m_iHead] = -1;
      // 最新一个node的索引
      m_iLastInsertPos = m_iHead;

      // 丢了多少个包，按序列号进行统计
      m_iLength += CSeqNo::seqlen(seqno1, seqno2);

      return m_iLength;
   }

   // otherwise find the position where the data can be inserted
   // sndLossList中已经有数据了，需要计算插入位置
   int origlen = m_iLength;
   // 新的起始序列号相较于head节点起始序列号的偏移量
   int offset = CSeqNo::seqoff(m_piData1[m_iHead], seqno1);
   // 计算插入位置
   int loc = (m_iHead + offset + m_iSize) % m_iSize;

   // offset < 0, 说明新的序列号比数组head节点的起始序列号还小
   // 需要将新的序列号插入到head节点之前，并更新head节点
   if (offset < 0)
   {
      // Insert data prior to the head pointer

      // 插入起始序列号
      m_piData1[loc] = seqno1;
      // 插入终止序列号
      if (seqno2 != seqno1)
         m_piData2[loc] = seqno2;

      // new node becomes head
      // 新的节点成为head节点，更新下一个节点的索引
      m_piNext[loc] = m_iHead;
      // 更新head节点索引
      m_iHead = loc;
      // 最新插入节点的索引
      m_iLastInsertPos = loc;

      // 丢包统计
      m_iLength += CSeqNo::seqlen(seqno1, seqno2);
   }
   // offset > 0, 说明新的序列号比head节点的序列号大
   else if (offset > 0)
   {
      // seqno1 == m_piData1[loc]说明loc位置已经有数据
      // 将loc位置的序列号和新的序列号进行比较，更新loc位置的序列号，看看是否需要进行合并
      if (seqno1 == m_piData1[loc])
      {
         // 最新节点的插入位置
         m_iLastInsertPos = loc;

         // first seqno is equivlent, compare the second
         // 起始序列号相同，比较第终止序列号
         // -1 == m_piData2[loc]说明loc位置的终止序列号不存在
         if (-1 == m_piData2[loc])
         {
            // 起始序列号和终止序列号不同，更新m_piData2；
            if (seqno2 != seqno1)
            {
               // 丢包统计
               m_iLength += CSeqNo::seqlen(seqno1, seqno2) - 1;
               // 更新终止序列号
               m_piData2[loc] = seqno2;
            }
         }
         // 终止序列号比loc位置的终止序列号大，进行合并
         else if (CSeqNo::seqcmp(seqno2, m_piData2[loc]) > 0)
         {
            // new seq pair is longer than old pair, e.g., insert [3, 7] to [3, 5], becomes [3, 7]
            // 丢包统计
            m_iLength += CSeqNo::seqlen(m_piData2[loc], seqno2) - 1;
            // 更新终止序列号
            m_piData2[loc] = seqno2;
         }
         else
            // Do nothing if it is already there
            return 0;
      }
      // loc位置没有数据，需要找到合适的插入位置
      else
      {
         // searching the prior node
         // 搜索优先节点，减少寻找插入位置时的遍历次数，这是一个性能优化的点，妙啊！
         int i;
         // 如果新的序列号比上次插入的序列号大，则从上次插入位置开始搜索
         if ((-1 != m_iLastInsertPos) && (CSeqNo::seqcmp(m_piData1[m_iLastInsertPos], seqno1) < 0))
            i = m_iLastInsertPos;
         // 否则从head节点开始搜索
         else
            i = m_iHead;

         // 查找插入位置
         while ((-1 != m_piNext[i]) && (CSeqNo::seqcmp(m_piData1[m_piNext[i]], seqno1) < 0))
            i = m_piNext[i];

         // 到这一步说明已经找到了合适的插入位置，检查是需要创建新的节点，还是与现有的节点合并
         // m_piData2尚未赋值，或者seqno2大于m_piData2中已保存的终止序列号，说明没有重叠的节点，需要新建节点
         if ((-1 == m_piData2[i]) || (CSeqNo::seqcmp(m_piData2[i], seqno1) < 0))
         {
            // 在loc位置创建新的节点，并调整节点索引

            // 记录最新的插入位置
            m_iLastInsertPos = loc;

            // no overlap, create new node
            // 没有重叠的节点，需要新建节点
            m_piData1[loc] = seqno1;
            if (seqno2 != seqno1)
               m_piData2[loc] = seqno2;

            // 更新下一个节点的索引
            m_piNext[loc] = m_piNext[i];
            m_piNext[i] = loc;
            
            // 丢包统计，按序列号进行统计
            m_iLength += CSeqNo::seqlen(seqno1, seqno2);
         }
         else
         {
            // 序列号范围有重叠，进行合并

            // 更新最新的插入位置
            m_iLastInsertPos = i;

            // overlap, coalesce with prior node, insert(3, 7) to [2, 5], ... becomes [2, 7]
            // 有重叠的节点，合并节点
            if (CSeqNo::seqcmp(m_piData2[i], seqno2) < 0)
            {
               // 丢包统计，按序列号进行统计
               m_iLength += CSeqNo::seqlen(m_piData2[i], seqno2) - 1;
               // 保存终止序列号
               m_piData2[i] = seqno2;

               loc = i;
            }
            else
               return 0;
         }
      }
   }
   // offset == 0, 说明新的序列号与head节点的序列号相同，直接返回
   else
   {
      m_iLastInsertPos = m_iHead;

      // insert to head node
      // seqno2 != seqno1说明不止一个丢包，需要更新loc位置的终止序列号
      if (seqno2 != seqno1)
      {
         // loc位置的终止序列号不存在，更新终止序列号
         if (-1 == m_piData2[loc])
         {
            m_iLength += CSeqNo::seqlen(seqno1, seqno2) - 1;
            m_piData2[loc] = seqno2;
         }
         // loc位置的终止序列号比新的序列号小，进行合并
         else if (CSeqNo::seqcmp(seqno2, m_piData2[loc]) > 0)
         {
            m_iLength += CSeqNo::seqlen(m_piData2[loc], seqno2) - 1;
            m_piData2[loc] = seqno2;
         }
         else 
            return 0;
      }
      else
         return 0;
   }

   // coalesce with next node. E.g., [3, 7], ..., [6, 9] becomes [3, 9] 
   // 检查并合并相邻或重叠的序列号范围，例：[3,7]，……，[6,9]变成[3,9]
   while ((-1 != m_piNext[loc]) && (-1 != m_piData2[loc]))
   {
      int i = m_piNext[loc];

      // 如果下一个节点的起始序列号小于等于loc位置的终止序列号，说明有重叠
      if (CSeqNo::seqcmp(m_piData1[i], CSeqNo::incseq(m_piData2[loc])) <= 0)
      {
         // coalesce if there is overlap
         // 下一个节点的终止序列号存在，进行合并
         if (-1 != m_piData2[i])
         {
            // 下一个节点的终止序列号大于当前节点的终止序列号，进行合并
            if (CSeqNo::seqcmp(m_piData2[i], m_piData2[loc]) > 0)
            {
               // 下一个节点的起始序列号小于等于当前节点的终止序列号
               if (CSeqNo::seqcmp(m_piData2[loc], m_piData1[i]) >= 0)
                  m_iLength -= CSeqNo::seqlen(m_piData1[i], m_piData2[loc]);

               // 更新当前节点的终止序列号
               m_piData2[loc] = m_piData2[i];
            }
            // 下一个节点的终止序列号小于等于当前节点的终止序列号
            else
               m_iLength -= CSeqNo::seqlen(m_piData1[i], m_piData2[i]); // 减去被合并的长度
         }
         // 下一个节点不存在终止序列号
         else
         {
            // 检查是否有相邻的序列号，即下一个节点的起始序列号等于当前节点的终止序列号加1
            if (m_piData1[i] == CSeqNo::incseq(m_piData2[loc]))
               m_piData2[loc] = m_piData1[i];
            else
               m_iLength --;
         }

         // 删除被合并的节点
         m_piData1[i] = -1;
         m_piData2[i] = -1;
         // 更新下一个节点的索引
         m_piNext[loc] = m_piNext[i];
      }
      else
         break;
   }

   // 返回丢包列表增加的长度
   return m_iLength - origlen;
}

/*
   1. 找到目标序列号seqno在数组中的位置loc
   2. 创建一个新的节点，新节点的起始序列号为seqno+1,终止序列号需要根据旧节点的情况进行计算
   3. 将这个新的节点作为头节点
   4. 删除所有新的头节点之前的数据
*/
void CSndLossList::remove(int32_t seqno)
{
   CGuard listguard(m_ListLock);

   if (0 == m_iLength)
      return;

   // Remove all from the head pointer to a node with a larger seq. no. or the list is empty
   // 计算从头节点起始序列号到目标序列号 seqno 的偏移量 offset
   int offset = CSeqNo::seqoff(m_piData1[m_iHead], seqno);
   // 计算目标序列号 seqno 在数组中的位置 loc
   int loc = (m_iHead + offset + m_iSize) % m_iSize;

   // 处理头节点匹配的情况，offset==0, 说明目标序列号seqno就是头节点的起始序列号
   // 构建一个新的节点，起始序列号为seqno+1,终止序列号仍然为就得值
   // 将新的节点作为头节点
   if (0 == offset)
   {
      // It is the head. Remove the head and point to the next node
      // 计算新的位置，删除loc之前的序列号
      loc = (loc + 1) % m_iSize;

      // 头节点没有终止序列号，说明只有一个序列号的包被丢弃了，直接更新loc
      if (-1 == m_piData2[m_iHead]){
         loc = m_piNext[m_iHead];
      }
      // 头节点有终止序列号
      else
      {
         // 构建一个新的节点，起始序列号为seqno+1;
         m_piData1[loc] = CSeqNo::incseq(seqno);
         // 新节点的终止序列号仍为旧值
         if (CSeqNo::seqcmp(m_piData2[m_iHead], CSeqNo::incseq(seqno)) > 0)
            m_piData2[loc] = m_piData2[m_iHead];

         // 清除原头节点
         m_piData2[m_iHead] = -1;
         // 更新新节点的下一个节点索引，维护链表结构
         m_piNext[loc] = m_piNext[m_iHead];
      }

      // 清除原头节点的起始序列
      m_piData1[m_iHead] = -1;
      // 更新最新插入位置的索引
      if (m_iLastInsertPos == m_iHead)
         m_iLastInsertPos = -1;

      // 更新头节点索引为新构建的节点
      m_iHead = loc;

      // 丢包统计
      m_iLength --;
   }
   // offset>0, 说明seqno不在头节点中
   else if (offset > 0)
   {
      // 保存原始头节点
      int h = m_iHead;

      // seqno恰好是某个节点的起始序列号
      if (seqno == m_piData1[loc])
      {
         // target node is not empty, remove part/all of the seqno in the node.
         // 保存当前节点索引
         int temp = loc;
         // 构建新节点
         loc = (loc + 1) % m_iSize;

         // 当前节点只有起始序列号，没有终止序列号，即当前节点只表示一个序列号
         if (-1 == m_piData2[temp]){
            // 更新头节点索引，直接跳过当前节点
            m_iHead = m_piNext[temp];
         }
         // 当前节点有终止序列号
         else
         {
            // remove part, e.g., [3, 7] becomes [], [4, 7] after remove(3)
            // 构建新节点，新节点的起始序列号为seqno+1
            m_piData1[loc] = CSeqNo::incseq(seqno);
            // 新节点的起始序列号小于当前节点的终止序列号,更新新节点的终止序列号
            if (CSeqNo::seqcmp(m_piData2[temp], m_piData1[loc]) > 0)
               m_piData2[loc] = m_piData2[temp];

            // 更新头节点索引
            m_iHead = loc;
            // 更新头节点的下一个节点的索引，维护链表结构
            m_piNext[loc] = m_piNext[temp];
            // 清除原节点信息
            m_piNext[temp] = loc;
            m_piData2[temp] = -1;
         }
      }
      // seqno在某个节点的序列号范围内
      else
      {
         // target node is empty, check prior node
         int i = m_iHead;
         // 查找seqno所在的节点
         while ((-1 != m_piNext[i]) && (CSeqNo::seqcmp(m_piData1[m_piNext[i]], seqno) < 0))
            i = m_piNext[i];

         // 构建新节点，只保存所有大于seqno的序列号
         loc = (loc + 1) % m_iSize;

         // 当前节点没有终止序列号的情况，说明seqno是当前节点的起始序列号
         if (-1 == m_piData2[i])
            m_iHead = m_piNext[i];
         // 当前节点的终止序列号大于seqno
         else if (CSeqNo::seqcmp(m_piData2[i], seqno) > 0)
         {
            // remove part/all seqno in the prior node
            // 新节点保存大于seqno的序列号
            m_piData1[loc] = CSeqNo::incseq(seqno);
            // 当前节点的终止序列号比新节点的起始序列号大，更新新节点的终止序列号
            if (CSeqNo::seqcmp(m_piData2[i], m_piData1[loc]) > 0)
               m_piData2[loc] = m_piData2[i];

            // 更新当前节点的终止序列号
            m_piData2[i] = seqno;

            // 维护链表结构
            m_piNext[loc] = m_piNext[i];
            m_piNext[i] = loc;

            m_iHead = loc;
         }
         else
            m_iHead = m_piNext[i];
      }

      // Remove all nodes prior to the new head
      // 清理所有在新的头节点之前的节点
      while (h != m_iHead)
      {
         if (m_piData2[h] != -1)
         {
            m_iLength -= CSeqNo::seqlen(m_piData1[h], m_piData2[h]);
            m_piData2[h] = -1;
         }
         else
            m_iLength --;

         m_piData1[h] = -1;

         if (m_iLastInsertPos == h)
            m_iLastInsertPos = -1;

         h = m_piNext[h];
      }
   }
}

int CSndLossList::getLossLength()
{
   CGuard listguard(m_ListLock);

   return m_iLength;
}

int32_t CSndLossList::getLostSeq()
{
   if (0 == m_iLength)
     return -1;

   CGuard listguard(m_ListLock);

   if (0 == m_iLength)
     return -1;

   if (m_iLastInsertPos == m_iHead)
      m_iLastInsertPos = -1;

   // return the first loss seq. no.
   // 获取头节点的起始序列号
   int32_t seqno = m_piData1[m_iHead];

   // head moves to the next node
   // 头节点的终止序列号不存在，说明只有一个序列号的包被丢弃了，直接更新头节点
   if (-1 == m_piData2[m_iHead])
   {
      //[3, -1] becomes [], and head moves to next node in the list
      m_piData1[m_iHead] = -1;
      m_iHead = m_piNext[m_iHead];
   }
   // 头节点的终止序列号存在，更新头节点
   else
   {
      // shift to next node, e.g., [3, 7] becomes [], [4, 7]
      int loc = (m_iHead + 1) % m_iSize;

      // 构建新节点，新节点的起始序列号为seqno+1
      m_piData1[loc] = CSeqNo::incseq(seqno);
      // 更新新节点的终止序列号
      if (CSeqNo::seqcmp(m_piData2[m_iHead], m_piData1[loc]) > 0)
         m_piData2[loc] = m_piData2[m_iHead];

      // 清除原头节点
      m_piData1[m_iHead] = -1;
      m_piData2[m_iHead] = -1;

      // 更新新节点的下一个节点索引，维护链表结构
      m_piNext[loc] = m_piNext[m_iHead];
      // 更新头节点索引为新节点
      m_iHead = loc;
   }

   m_iLength --;

   return seqno;
}

////////////////////////////////////////////////////////////////////////////////

CRcvLossList::CRcvLossList(int size):
m_piData1(NULL),
m_piData2(NULL),
m_piNext(NULL),
m_piPrior(NULL),
m_iHead(-1),
m_iTail(-1),
m_iLength(0),
m_iSize(size)
{
   m_piData1 = new int32_t [m_iSize];
   m_piData2 = new int32_t [m_iSize];
   m_piNext = new int [m_iSize];
   m_piPrior = new int [m_iSize];

   // -1 means there is no data in the node
   for (int i = 0; i < size; ++ i)
   {
      m_piData1[i] = -1;
      m_piData2[i] = -1;
   }
}

CRcvLossList::~CRcvLossList()
{
   delete [] m_piData1;
   delete [] m_piData2;
   delete [] m_piNext;
   delete [] m_piPrior;
}

void CRcvLossList::insert(int32_t seqno1, int32_t seqno2)
{
   // Data to be inserted must be larger than all those in the list
   // guaranteed by the UDT receiver

   if (0 == m_iLength)
   {
      // insert data into an empty list
      m_iHead = 0;
      m_iTail = 0;
      m_piData1[m_iHead] = seqno1;
      if (seqno2 != seqno1)
         m_piData2[m_iHead] = seqno2;

      m_piNext[m_iHead] = -1;
      m_piPrior[m_iHead] = -1;
      m_iLength += CSeqNo::seqlen(seqno1, seqno2);

      return;
   }

   // otherwise searching for the position where the node should be
   int offset = CSeqNo::seqoff(m_piData1[m_iHead], seqno1);
   int loc = (m_iHead + offset) % m_iSize;

   if ((-1 != m_piData2[m_iTail]) && (CSeqNo::incseq(m_piData2[m_iTail]) == seqno1))
   {
      // coalesce with prior node, e.g., [2, 5], [6, 7] becomes [2, 7]
      loc = m_iTail;
      m_piData2[loc] = seqno2;
   }
   else
   {
      // create new node
      m_piData1[loc] = seqno1;

      if (seqno2 != seqno1)
         m_piData2[loc] = seqno2;

      m_piNext[m_iTail] = loc;
      m_piPrior[loc] = m_iTail;
      m_piNext[loc] = -1;
      m_iTail = loc;
   }

   m_iLength += CSeqNo::seqlen(seqno1, seqno2);
}

bool CRcvLossList::remove(int32_t seqno)
{
   if (0 == m_iLength)
      return false; 

   // locate the position of "seqno" in the list
   int offset = CSeqNo::seqoff(m_piData1[m_iHead], seqno);
   if (offset < 0)
      return false;

   int loc = (m_iHead + offset) % m_iSize;

   if (seqno == m_piData1[loc])
   {
      // This is a seq. no. that starts the loss sequence

      if (-1 == m_piData2[loc])
      {
         // there is only 1 loss in the sequence, delete it from the node
         if (m_iHead == loc)
         {
            m_iHead = m_piNext[m_iHead];
            if (-1 != m_iHead)
               m_piPrior[m_iHead] = -1;
         }
         else
         {
            m_piNext[m_piPrior[loc]] = m_piNext[loc];
            if (-1 != m_piNext[loc])
               m_piPrior[m_piNext[loc]] = m_piPrior[loc];
            else
               m_iTail = m_piPrior[loc];
         }

         m_piData1[loc] = -1;
      }
      else
      {
         // there are more than 1 loss in the sequence
         // move the node to the next and update the starter as the next loss inSeqNo(seqno)

         // find next node
         int i = (loc + 1) % m_iSize;

         // remove the "seqno" and change the starter as next seq. no.
         m_piData1[i] = CSeqNo::incseq(m_piData1[loc]);

         // process the sequence end
         if (CSeqNo::seqcmp(m_piData2[loc], CSeqNo::incseq(m_piData1[loc])) > 0)
            m_piData2[i] = m_piData2[loc];

         // remove the current node
         m_piData1[loc] = -1;
         m_piData2[loc] = -1;
 
         // update list pointer
         m_piNext[i] = m_piNext[loc];
         m_piPrior[i] = m_piPrior[loc];

         if (m_iHead == loc)
            m_iHead = i;
         else
            m_piNext[m_piPrior[i]] = i;

         if (m_iTail == loc)
            m_iTail = i;
         else
            m_piPrior[m_piNext[i]] = i;
      }

      m_iLength --;

      return true;
   }

   // There is no loss sequence in the current position
   // the "seqno" may be contained in a previous node

   // searching previous node
   int i = (loc - 1 + m_iSize) % m_iSize;
   while (-1 == m_piData1[i])
      i = (i - 1 + m_iSize) % m_iSize;

   // not contained in this node, return
   if ((-1 == m_piData2[i]) || (CSeqNo::seqcmp(seqno, m_piData2[i]) > 0))
       return false;

   if (seqno == m_piData2[i])
   {
      // it is the sequence end

      if (seqno == CSeqNo::incseq(m_piData1[i]))
         m_piData2[i] = -1;
      else
         m_piData2[i] = CSeqNo::decseq(seqno);
   }
   else
   {
      // split the sequence

      // construct the second sequence from CSeqNo::incseq(seqno) to the original sequence end
      // located at "loc + 1"
      loc = (loc + 1) % m_iSize;

      m_piData1[loc] = CSeqNo::incseq(seqno);
      if (CSeqNo::seqcmp(m_piData2[i], m_piData1[loc]) > 0)      
         m_piData2[loc] = m_piData2[i];

      // the first (original) sequence is between the original sequence start to CSeqNo::decseq(seqno)
      if (seqno == CSeqNo::incseq(m_piData1[i]))
         m_piData2[i] = -1;
      else
         m_piData2[i] = CSeqNo::decseq(seqno);

      // update the list pointer
      m_piNext[loc] = m_piNext[i];
      m_piNext[i] = loc;
      m_piPrior[loc] = i;

      if (m_iTail == i)
         m_iTail = loc;
      else
         m_piPrior[m_piNext[loc]] = loc;
   }

   m_iLength --;

   return true;
}

bool CRcvLossList::remove(int32_t seqno1, int32_t seqno2)
{
   if (seqno1 <= seqno2)
   {
      for (int32_t i = seqno1; i <= seqno2; ++ i)
         remove(i);
   }
   else
   {
      for (int32_t j = seqno1; j < CSeqNo::m_iMaxSeqNo; ++ j)
         remove(j);
      for (int32_t k = 0; k <= seqno2; ++ k)
         remove(k);
   }

   return true;
}

bool CRcvLossList::find(int32_t seqno1, int32_t seqno2) const
{
   if (0 == m_iLength)
      return false;

   int p = m_iHead;

   while (-1 != p)
   {
      if ((CSeqNo::seqcmp(m_piData1[p], seqno1) == 0) ||
          ((CSeqNo::seqcmp(m_piData1[p], seqno1) > 0) && (CSeqNo::seqcmp(m_piData1[p], seqno2) <= 0)) ||
          ((CSeqNo::seqcmp(m_piData1[p], seqno1) < 0) && (m_piData2[p] != -1) && CSeqNo::seqcmp(m_piData2[p], seqno1) >= 0))
          return true;

      p = m_piNext[p];
   }

   return false;
}

int CRcvLossList::getLossLength() const
{
   return m_iLength;
}

int CRcvLossList::getFirstLostSeq() const
{
   if (0 == m_iLength)
      return -1;

   return m_piData1[m_iHead];
}

void CRcvLossList::getLossArray(int32_t* array, int& len, int limit)
{
   len = 0;

   int i = m_iHead;

   while ((len < limit - 1) && (-1 != i))
   {
      array[len] = m_piData1[i];
      if (-1 != m_piData2[i])
      {
         // there are more than 1 loss in the sequence
         array[len] |= 0x80000000;
         ++ len;
         array[len] = m_piData2[i];
      }

      ++ len;

      i = m_piNext[i];
   }
}
