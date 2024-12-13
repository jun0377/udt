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
   Yunhong Gu, last updated 01/01/2011
*****************************************************************************/

#ifdef LINUX
   #include <sys/epoll.h>
   #include <unistd.h>
#endif
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iterator>

#include "common.h"
#include "epoll.h"
#include "udt.h"

using namespace std;

CEPoll::CEPoll():
m_iIDSeed(0)
{
   // 初始化互斥锁
   CGuard::createMutex(m_EPollLock);
}

CEPoll::~CEPoll()
{
   // 释放互斥锁
   CGuard::releaseMutex(m_EPollLock);
}

int CEPoll::create()
{
   // lock_guard
   CGuard pg(m_EPollLock);

   int localid = 0;

   #ifdef LINUX
  
   // 调用系统API创建一个epoll实例,epoll_create的参数已经无效了
   localid = epoll_create(1024);
   if (localid < 0)
      throw CUDTException(-1, 0, errno);
   #else
   // on BSD, use kqueue
   // on Solaris, use /dev/poll
   // on Windows, select
   #endif

   if (++ m_iIDSeed >= 0x7FFFFFFF)
      m_iIDSeed = 0;

   // 描述一个epoll实例
   CEPollDesc desc;
   // map中的key
   desc.m_iID = m_iIDSeed;
   // 初始化本地实例为0
   desc.m_iLocalID = localid;
   // 插入到map中
   m_mPolls[desc.m_iID] = desc;

   return desc.m_iID;
}

int CEPoll::add_usock(const int eid, const UDTSOCKET& u, const int* events)
{
   // lock_guard
   CGuard pg(m_EPollLock);

   // 从map中查找epoll实例，不存在则抛出异常
   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(5, 13);

   // 插入到等待可读事件的队列中
   if (!events || (*events & UDT_EPOLL_IN))
      p->second.m_sUDTSocksIn.insert(u);
   // 插入到等待可写时间的队列中
   if (!events || (*events & UDT_EPOLL_OUT))
      p->second.m_sUDTSocksOut.insert(u);

   return 0;
}

int CEPoll::add_ssock(const int eid, const SYSSOCKET& s, const int* events)
{
   // lock_guard
   CGuard pg(m_EPollLock);

   // 从map中查找epoll实例
   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(5, 13);

   // 添加系统套接字到epoll实例中
#ifdef LINUX
   epoll_event ev;
   memset(&ev, 0, sizeof(epoll_event));

   if (NULL == events)
      ev.events = EPOLLIN | EPOLLOUT | EPOLLERR;
   else
   {
      ev.events = 0;
      if (*events & UDT_EPOLL_IN)
         ev.events |= EPOLLIN;
      if (*events & UDT_EPOLL_OUT)
         ev.events |= EPOLLOUT;
      if (*events & UDT_EPOLL_ERR)
         ev.events |= EPOLLERR;
   }

   ev.data.fd = s;
   if (::epoll_ctl(p->second.m_iLocalID, EPOLL_CTL_ADD, s, &ev) < 0)
      throw CUDTException();
#endif

   p->second.m_sLocals.insert(s);

   return 0;
}

int CEPoll::remove_usock(const int eid, const UDTSOCKET& u)
{
   // lock_guard
   CGuard pg(m_EPollLock);

   // 从map中查找epoll实例
   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(5, 13);

   // 清空相应的读/写/异常套接字集合
   p->second.m_sUDTSocksIn.erase(u);
   p->second.m_sUDTSocksOut.erase(u);
   p->second.m_sUDTSocksEx.erase(u);

   return 0;
}

int CEPoll::remove_ssock(const int eid, const SYSSOCKET& s)
{
   // lock_guard
   CGuard pg(m_EPollLock);

   // 从map中查找epoll实例
   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(5, 13);

   // 调用系统API删除系统套接字
#ifdef LINUX
   epoll_event ev;  // ev is ignored, for compatibility with old Linux kernel only.
   if (::epoll_ctl(p->second.m_iLocalID, EPOLL_CTL_DEL, s, &ev) < 0)
      throw CUDTException();
#endif

   // 从系统套接字集合中删除
   p->second.m_sLocals.erase(s);

   return 0;
}

int CEPoll::wait(const int eid, set<UDTSOCKET>* readfds, set<UDTSOCKET>* writefds, int64_t msTimeOut, set<SYSSOCKET>* lrfds, set<SYSSOCKET>* lwfds)
{
   // if all fields is NULL and waiting time is infinite, then this would be a deadlock
   // 参数判断
   if (!readfds && !writefds && !lrfds && lwfds && (msTimeOut < 0))
      throw CUDTException(5, 3, 0);

   // Clear these sets in case the app forget to do it.
   // 清空所有套接字集合
   if (readfds) readfds->clear();
   if (writefds) writefds->clear();
   if (lrfds) lrfds->clear();
   if (lwfds) lwfds->clear();

   // 记录已准备好的套接字
   int total = 0;

   // 获取当前时间
   int64_t entertime = CTimer::getTime();
   while (true)
   {
      // 临界区保护
      CGuard::enterCS(m_EPollLock);

      // 查找epoll实例
      map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
      if (p == m_mPolls.end())
      {
         CGuard::leaveCS(m_EPollLock);
         throw CUDTException(5, 13);
      }

      // 没有需要关注的套接字，抛出异常
      if (p->second.m_sUDTSocksIn.empty() && p->second.m_sUDTSocksOut.empty() && p->second.m_sLocals.empty() && (msTimeOut < 0))
      {
         // no socket is being monitored, this may be a deadlock
         CGuard::leaveCS(m_EPollLock);
         throw CUDTException(5, 3);
      }

      // Sockets with exceptions are returned to both read and write sets.
      // 有可读或异常套接字
      if ((NULL != readfds) && (!p->second.m_sUDTReads.empty() || !p->second.m_sUDTExcepts.empty()))
      {
         // 向外传出可读套接字
         *readfds = p->second.m_sUDTReads;
         // 异常套接字也需要传出
         for (set<UDTSOCKET>::const_iterator i = p->second.m_sUDTExcepts.begin(); i != p->second.m_sUDTExcepts.end(); ++ i)
            readfds->insert(*i);
         // 记录已准备好的套接字数量
         total += p->second.m_sUDTReads.size() + p->second.m_sUDTExcepts.size();
      }
      // 有可写或异常套接字
      if ((NULL != writefds) && (!p->second.m_sUDTWrites.empty() || !p->second.m_sUDTExcepts.empty()))
      {
         // 向外传出可写套接字
         *writefds = p->second.m_sUDTWrites;
         // 异常套接字也需要传出
         for (set<UDTSOCKET>::const_iterator i = p->second.m_sUDTExcepts.begin(); i != p->second.m_sUDTExcepts.end(); ++ i)
            writefds->insert(*i);
         // 记录已准备好的套接字数量
         total += p->second.m_sUDTWrites.size() + p->second.m_sUDTExcepts.size();
      }

      //　需要监听系统套接字的读/写事件,直接调用系统API epoll_wait即可
      if (lrfds || lwfds)
      {
#ifdef LINUX
         // 关注的系统套接字数量
         const int max_events = p->second.m_sLocals.size();
         // 返回的epoll_event数组
         epoll_event ev[max_events];
         // epoll_wait,不设超时时间，阻塞等待
         int nfds = ::epoll_wait(p->second.m_iLocalID, ev, max_events, 0);

         // 返回的epoll_event
         for (int i = 0; i < nfds; ++ i)
         {
            // 可读事件
            if ((NULL != lrfds) && (ev[i].events & EPOLLIN))
            {
               // 向外传出可读套接字
               lrfds->insert(ev[i].data.fd);
               ++ total;
            }
            // 可写事件
            if ((NULL != lwfds) && (ev[i].events & EPOLLOUT))
            {
               // 向外传出可写套接字
               lwfds->insert(ev[i].data.fd);
               ++ total;
            }
         }
#else
         //currently "select" is used for all non-Linux platforms.
         //faster approaches can be applied for specific systems in the future.

         //"select" has a limitation on the number of sockets

         fd_set readfds;
         fd_set writefds;
         FD_ZERO(&readfds);
         FD_ZERO(&writefds);

         for (set<SYSSOCKET>::const_iterator i = p->second.m_sLocals.begin(); i != p->second.m_sLocals.end(); ++ i)
         {
            if (lrfds)
               FD_SET(*i, &readfds);
            if (lwfds)
               FD_SET(*i, &writefds);
         }

         timeval tv;
         tv.tv_sec = 0;
         tv.tv_usec = 0;
         if (::select(0, &readfds, &writefds, NULL, &tv) > 0)
         {
            for (set<SYSSOCKET>::const_iterator i = p->second.m_sLocals.begin(); i != p->second.m_sLocals.end(); ++ i)
            {
               if (lrfds && FD_ISSET(*i, &readfds))
               {
                  lrfds->insert(*i);
                  ++ total;
               }
               if (lwfds && FD_ISSET(*i, &writefds))
               {
                  lwfds->insert(*i);
                  ++ total;
               }
            }
         }
#endif
      }

      // 退出临界区
      CGuard::leaveCS(m_EPollLock);

      if (total > 0)
         return total;

      // 超时，抛出异常
      if ((msTimeOut >= 0) && (int64_t(CTimer::getTime() - entertime) >= msTimeOut * 1000LL))
         throw CUDTException(6, 3, 0);

      // 阻塞等待，最多等待10ms
      CTimer::waitForEvent();
   }

   return 0;
}

int CEPoll::release(const int eid)
{
   // lock_guard
   CGuard pg(m_EPollLock);

   // 查找epoll实例
   map<int, CEPollDesc>::iterator i = m_mPolls.find(eid);
   if (i == m_mPolls.end())
      throw CUDTException(5, 13);

   #ifdef LINUX
   // release local/system epoll descriptor
   // 调用系统API关闭epoll实例
   ::close(i->second.m_iLocalID);
   #endif

   // 从map中清除对应的epoll实例
   m_mPolls.erase(i);

   return 0;
}

namespace
{

void update_epoll_sets(const UDTSOCKET& uid, const set<UDTSOCKET>& watch, set<UDTSOCKET>& result, bool enable)
{
   // 插入套接字
   if (enable && (watch.find(uid) != watch.end()))
   {
      result.insert(uid);
   }
   // 删除套接字
   else if (!enable)
   {
      result.erase(uid);
   }
}

}  // namespace

// 更新udt sockfd关注的epoll事件
int CEPoll::update_events(const UDTSOCKET& uid, std::set<int>& eids, int events, bool enable)
{
   // lock_guard
   CGuard pg(m_EPollLock);

   map<int, CEPollDesc>::iterator p;
   
   // 记录已删除的epoll实例
   vector<int> lost;
   // UDT套接字uid关联的所有epoll实例
   for (set<int>::iterator i = eids.begin(); i != eids.end(); ++ i)
   {
      // 查找指定的epoll实例
      p = m_mPolls.find(*i);
      // 没有在map中找到对应的epoll实例，说明已经被删除了
      if (p == m_mPolls.end())
      {
         // 记录已删除的epoll实例
         lost.push_back(*i);
      }
      else
      {
         // 加入可读事件的就绪集合
         if ((events & UDT_EPOLL_IN) != 0)
            update_epoll_sets(uid, p->second.m_sUDTSocksIn, p->second.m_sUDTReads, enable);
         // 加入可写事件的就绪集合
         if ((events & UDT_EPOLL_OUT) != 0)
            update_epoll_sets(uid, p->second.m_sUDTSocksOut, p->second.m_sUDTWrites, enable);
         // 加入异常事件的就绪集合
         if ((events & UDT_EPOLL_ERR) != 0)
            update_epoll_sets(uid, p->second.m_sUDTSocksEx, p->second.m_sUDTExcepts, enable);
      }
   }

   // 从epoll实例中删除已失效的UDT实例
   for (vector<int>::iterator i = lost.begin(); i != lost.end(); ++ i)
      eids.erase(*i);

   return 0;
}
