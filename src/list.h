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

#ifndef __UDT_LIST_H__
#define __UDT_LIST_H__


#include "udt.h"
#include "common.h"


// 重传队列，记录传输中丢的包，用于重传
// 使用两个list来保存丢包的起始序列号和终止序列号
class CSndLossList
{
public:
   CSndLossList(int size = 1024);
   ~CSndLossList();

      // Functionality:
      //    Insert a seq. no. into the sender loss list.
      // Parameters:
      //    0) [in] seqno1: sequence number starts.
      //    1) [in] seqno2: sequence number ends.
      // Returned value:
      //    number of packets that are not in the list previously.

   // 插入一对序列号，表示丢包范围
   int insert(int32_t seqno1, int32_t seqno2);

      // Functionality:
      //    Remove ALL the seq. no. that are not greater than the parameter.
      // Parameters:
      //    0) [in] seqno: sequence number.
      // Returned value:
      //    None.
   
   // 删除所有小于等于seqno的序列号
   void remove(int32_t seqno);

      // Functionality:
      //    Read the loss length.
      // Parameters:
      //    None.
      // Returned value:
      //    The length of the list.

   // 获取丢包数量，按序列号进行统计
   int getLossLength();

      // Functionality:
      //    Read the first (smallest) loss seq. no. in the list and remove it.
      // Parameters:
      //    None.
      // Returned value:
      //    The seq. no. or -1 if the list is empty.
   
   // 获取最小序列号，并从重传列表中删除该序列号
   int32_t getLostSeq();

private:
   // 数组，记录起始序列号
   int32_t* m_piData1;                  // sequence number starts
   // 数组，记录终止序列号
   int32_t* m_piData2;                  // seqnence number ends
   // 数组，极了下一个节点在 m_piData1  和 m_piData2 中的位置
   int* m_piNext;                       // next node in the list

   // 第一个节点的索引
   int m_iHead;                         // first node
   // 丢了多少个包，按序列号进行统计
   int m_iLength;                       // loss length
   // 静态数组的大小
   int m_iSize;                         // size of the static array
   // 最新一次插入的位置
   int m_iLastInsertPos;                // position of last insert node

   pthread_mutex_t m_ListLock;          // used to synchronize list operation

private:
   // 禁用拷贝构造和赋值运算符
   CSndLossList(const CSndLossList&);
   CSndLossList& operator=(const CSndLossList&);
};

////////////////////////////////////////////////////////////////////////////////

class CRcvLossList
{
public:
   CRcvLossList(int size = 1024);
   ~CRcvLossList();

      // Functionality:
      //    Insert a series of loss seq. no. between "seqno1" and "seqno2" into the receiver's loss list.
      // Parameters:
      //    0) [in] seqno1: sequence number starts.
      //    1) [in] seqno2: seqeunce number ends.
      // Returned value:
      //    None.

   // 插入一对序列号，表示丢包范围
   void insert(int32_t seqno1, int32_t seqno2);

      // Functionality:
      //    Remove a loss seq. no. from the receiver's loss list.
      // Parameters:
      //    0) [in] seqno: sequence number.
      // Returned value:
      //    if the packet is removed (true) or no such lost packet is found (false).

   // 删除一个序列号
   bool remove(int32_t seqno);

      // Functionality:
      //    Remove all packets between seqno1 and seqno2.
      // Parameters:
      //    0) [in] seqno1: start sequence number.
      //    1) [in] seqno2: end sequence number.
      // Returned value:
      //    if the packet is removed (true) or no such lost packet is found (false).

   // 删除一个序列号范围
   bool remove(int32_t seqno1, int32_t seqno2);

      // Functionality:
      //    Find if there is any lost packets whose sequence number falling seqno1 and seqno2.
      // Parameters:
      //    0) [in] seqno1: start sequence number.
      //    1) [in] seqno2: end sequence number.
      // Returned value:
      //    True if found; otherwise false.

   // 查找一个序列号范围
   bool find(int32_t seqno1, int32_t seqno2) const;

      // Functionality:
      //    Read the loss length.
      // Parameters:
      //    None.
      // Returned value:
      //    the length of the list.

   // 获取丢包数量
   int getLossLength() const;

      // Functionality:
      //    Read the first (smallest) seq. no. in the list.
      // Parameters:
      //    None.
      // Returned value:
      //    the sequence number or -1 if the list is empty.

   // 获取最小序列号
   int getFirstLostSeq() const;

      // Functionality:
      //    Get a encoded loss array for NAK report.
      // Parameters:
      //    0) [out] array: the result list of seq. no. to be included in NAK.
      //    1) [out] physical length of the result array.
      //    2) [in] limit: maximum length of the array.
      // Returned value:
      //    None.

   // 获取丢包数组
   void getLossArray(int32_t* array, int& len, int limit);

private:
   // 数组，记录起始序列号
   int32_t* m_piData1;                  // sequence number starts
   // 数组，记录终止序列号
   int32_t* m_piData2;                  // sequence number ends
   // 数组，记录下一个节点在 m_piData1 和 m_piData2 中的位置
   int* m_piNext;                       // next node in the list
   // 数组，记录前一个节点在 m_piData1 和 m_piData2 中的位置
   int* m_piPrior;                      // prior node in the list;

   // 第一个节点的索引
   int m_iHead;                         // first node in the list
   // 最后一个节点的索引
   int m_iTail;                         // last node in the list;
   // 丢包数量
   int m_iLength;                       // loss length
   // 静态数组的大小
   int m_iSize;                         // size of the static array

private:
   CRcvLossList(const CRcvLossList&);
   CRcvLossList& operator=(const CRcvLossList&);
};


#endif
