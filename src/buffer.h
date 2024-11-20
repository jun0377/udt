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
   Yunhong Gu, last updated 05/05/2009
*****************************************************************************/

#ifndef __UDT_BUFFER_H__
#define __UDT_BUFFER_H__


#include "udt.h"
#include "list.h"
#include "queue.h"
#include <fstream>

class CSndBuffer
{
public:
   CSndBuffer(int size = 32, int mss = 1500);
   ~CSndBuffer();

      // Functionality:
      //    Insert a user buffer into the sending list.
      // Parameters:
      //    0) [in] data: pointer to the user data block.
      //    1) [in] len: size of the block.
      //    2) [in] ttl: time to live in milliseconds
      //    3) [in] order: if the block should be delivered in order, for DGRAM only
      // Returned value:
      //    None.

   // 向发送列表中插入一个用户数据块
   void addBuffer(const char* data, int len, int ttl = -1, bool order = false);

      // Functionality:
      //    Read a block of data from file and insert it into the sending list.
      // Parameters:
      //    0) [in] ifs: input file stream.
      //    1) [in] len: size of the block.
      // Returned value:
      //    actual size of data added from the file.

   //从文件中读取数据块并插入到发送列表中
   int addBufferFromFile(std::fstream& ifs, int len);

      // Functionality:
      //    Find data position to pack a DATA packet from the furthest reading point.
      // Parameters:
      //    0) [out] data: the pointer to the data position.
      //    1) [out] msgno: message number of the packet.
      // Returned value:
      //    Actual length of data read.

   // 从发送列表中读取数据块，按块读取，每次不一定会读取一个完整的消息
   int readData(char** data, int32_t& msgno);

      // Functionality:
      //    Find data position to pack a DATA packet for a retransmission.
      // Parameters:
      //    0) [out] data: the pointer to the data position.
      //    1) [in] offset: offset from the last ACK point.
      //    2) [out] msgno: message number of the packet.
      //    3) [out] msglen: length of the message
      // Returned value:
      //    Actual length of data read.

   // 按偏移量从发送缓冲区中读数据，并且丢弃超时未发送的数据
   int readData(char** data, const int offset, int32_t& msgno, int& msglen);

      // Functionality:
      //    Update the ACK point and may release/unmap/return the user data according to the flag.
      // Parameters:
      //    0) [in] offset: number of packets acknowledged.
      // Returned value:
      //    None.

   // 丢弃已被确认的数据
   void ackData(int offset);

      // Functionality:
      //    Read size of data still in the sending list.
      // Parameters:
      //    None.
      // Returned value:
      //    Current size of the data in the sending list.

   // 发送队列中的数据大小
   int getCurrBufSize() const;

private:
   // 动态增大发送缓冲区
   void increase();

private:
   // 用于同步操作
   pthread_mutex_t m_BufLock;           // used to synchronize buffer operation

   struct Block
   {
      // 指向数据块的指针
      char* m_pcData;                   // pointer to the data block
      // 数据块中有效数据的大小，有些数据可能并不会占用一个完整的数据块
      int m_iLength;                    // length of the block

      // 消息编号，用来区分是不是一个完整的消息和是否需要按序发送
      int32_t m_iMsgNo;                 // message number
      // 数据被放入发送缓冲区时的时间戳
      uint64_t m_OriginTime;            // original request time
      // 数据生存时间，ms
      int m_iTTL;                       // time to live (milliseconds)

      Block* m_pNext;                   // next block
   } *m_pBlock, *m_pFirstBlock, *m_pCurrBlock, *m_pLastBlock;

   // m_pBlock:         The head pointer
   // m_pFirstBlock:    The first block
   // m_pCurrBlock:	The current block
   // m_pLastBlock:     The last block (if first == last, buffer is empty)

   struct Buffer
   {
      char* m_pcData;			// buffer
      int m_iSize;			// size
      Buffer* m_pNext;			// next buffer
   } *m_pBuffer;			// physical buffer

   int32_t m_iNextMsgNo;                // next message number

   // 实际分配的内存 = 数据块个数m_iSize * 最大包大小m_iMSS
   // 数据块的个数
   int m_iSize;				// buffer size (number of packets)
   // 最大报文段/最大包大小,也是每个数据块的长度
   int m_iMSS;                          // maximum seqment/packet size

   // 已使用的数据块
   int m_iCount;			// number of used blocks

private:
   // 仅声明未实现，相当于删除了拷贝构造函数
   CSndBuffer(const CSndBuffer&);
   // 仅声明未实现，相当于仅用了拷贝赋值操作
   CSndBuffer& operator=(const CSndBuffer&);
};

////////////////////////////////////////////////////////////////////////////////

// 使用一个环形缓冲区来管理接收到的数据，进行数据的接收、缓存、读取、确认等操作
class CRcvBuffer
{
public:
   // 初始化缓冲区大小和队列指针，并分配内存
   CRcvBuffer(CUnitQueue* queue, int bufsize = 65536);
   // 释放缓冲区中所有数据单元，清空指针数组
   ~CRcvBuffer();

      // Functionality:
      //    Write data into the buffer.
      // Parameters:
      //    0) [in] unit: pointer to a data unit containing new packet
      //    1) [in] offset: offset from last ACK point.
      // Returned value:
      //    0 is success, -1 if data is repeated.

   // 向接收缓冲区中写数据，每次写入一个数据单元
   int addData(CUnit* unit, int offset);

      // Functionality:
      //    Read data into a user buffer.
      // Parameters:
      //    0) [in] data: pointer to user buffer.
      //    1) [in] len: length of user buffer.
      // Returned value:
      //    size of data read.

   // 从接收缓冲区中读数据
   int readBuffer(char* data, int len);

      // Functionality:
      //    Read data directly into file.
      // Parameters:
      //    0) [in] file: C++ file stream.
      //    1) [in] len: expected length of data to write into the file.
      // Returned value:
      //    size of data read.

   // 从接收缓冲区中读数据到文件
   int readBufferToFile(std::fstream& ofs, int len);

      // Functionality:
      //    Update the ACK point of the buffer.
      // Parameters:
      //    0) [in] len: size of data to be acknowledged.
      // Returned value:
      //    1 if a user buffer is fulfilled, otherwise 0.

   // 已ACK确认数据
   void ackData(int len);

      // Functionality:
      //    Query how many buffer space left for data receiving.
      // Parameters:
      //    None.
      // Returned value:
      //    size of available buffer space (including user buffer) for data receiving.
   
   // 缓冲区中还有多少空间可用
   int getAvailBufSize() const;

      // Functionality:
      //    Query how many data has been continuously received (for reading).
      // Parameters:
      //    None.
      // Returned value:
      //    size of valid (continous) data for reading.

   // 缓冲区中有多少数据可读
   int getRcvDataSize() const;

      // Functionality:
      //    mark the message to be dropped from the message list.
      // Parameters:
      //    0) [in] msgno: message nuumer.
      // Returned value:
      //    None.

   // 标记需要丢弃的消息
   void dropMsg(int32_t msgno);

      // Functionality:
      //    read a message.
      // Parameters:
      //    0) [out] data: buffer to write the message into.
      //    1) [in] len: size of the buffer.
      // Returned value:
      //    actuall size of data read.

   // 读取一条消息
   int readMsg(char* data, int len);

      // Functionality:
      //    Query how many messages are available now.
      // Parameters:
      //    None.
      // Returned value:
      //    number of messages available for recvmsg.

   // 查询有多少条消息可以读取
   int getRcvMsgNum();

private:
   // 扫描缓冲区，查找完整的消息
   bool scanMsg(int& start, int& end, bool& passack);

private:
   // 指向缓冲区的指针数组，数组中的每个元素都指向一个数据单元
   CUnit** m_pUnit;                     // pointer to the protocol buffer
   // 缓冲区大小，即包含的数据单元数
   int m_iSize;                         // size of the protocol buffer
   // 数据单元队列，用于管理数据单元的分配和释放
   CUnitQueue* m_pUnitQueue;		// the shared unit queue

   // 缓冲区中数据的起始位置
   int m_iStartPos;                     // the head position for I/O (inclusive)
   // 缓冲区最后一个被ACK确认的数据位置
   int m_iLastAckPos;                   // the last ACKed position (exclusive)
					// EMPTY: m_iStartPos = m_iLastAckPos   FULL: m_iStartPos = m_iLastAckPos + 1
   // 最远数据位置，表示缓冲区中数据的最大偏移量，空：m_iStartPos = m_iLastAckPos； 满：m_iStartPos = m_iLastAckPos + 1
   int m_iMaxPos;			// the furthest data position
   // 在处理当前单元时，m_iNotch 用于记录已经读取了多少数据。这样，如果一次读取没有读取完整个单元的数据，下次可以从这个偏移量继
   int m_iNotch;			// the starting read point of the first unit

private:
   // 构造函数私有化，用于单例模式
   CRcvBuffer();
   // 仅声明未实现，相当于禁用了拷贝赋值操作
   CRcvBuffer(const CRcvBuffer&);
   // 仅声明未实现，相当于禁用了赋值操作
   CRcvBuffer& operator=(const CRcvBuffer&);
};


#endif
