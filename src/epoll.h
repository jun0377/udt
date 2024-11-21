/*****************************************************************************
Copyright (c) 2001 - 2010, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu, last updated 08/20/2010
*****************************************************************************/

#ifndef __UDT_EPOLL_H__
#define __UDT_EPOLL_H__


#include <map>
#include <set>
#include "udt.h"

// 一个epoll实例相关的sockfd集合
struct CEPollDesc
{
   // epoll实例
   int m_iID;                                // epoll ID
   // 等待可写事件的sockfd
   std::set<UDTSOCKET> m_sUDTSocksOut;       // set of UDT sockets waiting for write events
   // 等待可读事件的sockfd
   std::set<UDTSOCKET> m_sUDTSocksIn;        // set of UDT sockets waiting for read events
   // 关注异常事件的sockfd
   std::set<UDTSOCKET> m_sUDTSocksEx;        // set of UDT sockets waiting for exceptions

   // 系统API创建的epoll实例标识
   int m_iLocalID;                           // local system epoll ID
   // 系统套接字，不是UDT套接字
   std::set<SYSSOCKET> m_sLocals;            // set of local (non-UDT) descriptors

   // 可写的sockfd
   std::set<UDTSOCKET> m_sUDTWrites;         // UDT sockets ready for write
   // 可读的sockfd
   std::set<UDTSOCKET> m_sUDTReads;          // UDT sockets ready for read
   // 异常的sockfd
   std::set<UDTSOCKET> m_sUDTExcepts;        // UDT sockets with exceptions (connection broken, etc.)
};

class CEPoll
{
friend class CUDT;
friend class CRendezvousQueue;

public:
   CEPoll();
   ~CEPoll();

public: // for CUDTUnited API

      // Functionality:
      //    create a new EPoll.
      // Parameters:
      //    None.
      // Returned value:
      //    new EPoll ID if success, otherwise an error number.

   // 调用系统API epoll_create创建一个epoll实例，并将其添加到map中
   int create();

      // Functionality:
      //    add a UDT socket to an EPoll.
      // Parameters:
      //    0) [in] eid: EPoll ID.
      //    1) [in] u: UDT Socket ID.
      //    2) [in] events: events to watch.
      // Returned value:
      //    0 if success, otherwise an error number.

   // 向UDT epoll实例中添加udt sockfd；根据所关注的不同事件，插入到相应的队列中
   int add_usock(const int eid, const UDTSOCKET& u, const int* events = NULL);

      // Functionality:
      //    add a system socket to an EPoll.
      // Parameters:
      //    0) [in] eid: EPoll ID.
      //    1) [in] s: system Socket ID.
      //    2) [in] events: events to watch.
      // Returned value:
      //    0 if success, otherwise an error number.

   // 向epoll实例中添加系统sockfd
   int add_ssock(const int eid, const SYSSOCKET& s, const int* events = NULL);

      // Functionality:
      //    remove a UDT socket event from an EPoll; socket will be removed if no events to watch
      // Parameters:
      //    0) [in] eid: EPoll ID.
      //    1) [in] u: UDT socket ID.
      // Returned value:
      //    0 if success, otherwise an error number.

   // 从epoll实例中删除udt sockfd
   int remove_usock(const int eid, const UDTSOCKET& u);

      // Functionality:
      //    remove a system socket event from an EPoll; socket will be removed if no events to watch
      // Parameters:
      //    0) [in] eid: EPoll ID.
      //    1) [in] s: system socket ID.
      // Returned value:
      //    0 if success, otherwise an error number.

   // 从epoll实例中删除系统sockfd
   int remove_ssock(const int eid, const SYSSOCKET& s);

      // Functionality:
      //    wait for EPoll events or timeout.
      // Parameters:
      //    0) [in] eid: EPoll ID.
      //    1) [out] readfds: UDT sockets available for reading.
      //    2) [out] writefds: UDT sockets available for writing.
      //    3) [in] msTimeOut: timeout threshold, in milliseconds.
      //    4) [out] lrfds: system file descriptors for reading.
      //    5) [out] lwfds: system file descriptors for writing.
      // Returned value:
      //    number of sockets available for IO.

   // 等待epoll事件或超时；
   int wait(const int eid, std::set<UDTSOCKET>* readfds, std::set<UDTSOCKET>* writefds, int64_t msTimeOut, std::set<SYSSOCKET>* lrfds, std::set<SYSSOCKET>* lwfds);

      // Functionality:
      //    close and release an EPoll.
      // Parameters:
      //    0) [in] eid: EPoll ID.
      // Returned value:
      //    0 if success, otherwise an error number.

   // 释放epoll实例
   int release(const int eid);

public: // for CUDT to acknowledge IO status

      // Functionality:
      //    Update events available for a UDT socket.
      // Parameters:
      //    0) [in] uid: UDT socket ID.
      //    1) [in] eids: EPoll IDs to be set
      //    1) [in] events: Combination of events to update
      //    1) [in] enable: true -> enable, otherwise disable
      // Returned value:
      //    0 if success, otherwise an error number

   // 更新udt sockfd关注的epoll事件
   int update_events(const UDTSOCKET& uid, std::set<int>& eids, int events, bool enable);

private:
   // epoll实例的标识
   int m_iIDSeed;                            // seed to generate a new ID
   pthread_mutex_t m_SeedLock;

   // 使用一个map来保存所有的UDT epoll实例，一个UDT套接字可能被多个epoll实例监听
   std::map<int, CEPollDesc> m_mPolls;       // all epolls
   // 同步访问
   pthread_mutex_t m_EPollLock;
};


#endif
