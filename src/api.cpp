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
   Yunhong Gu, last updated 07/09/2011
*****************************************************************************/

#ifdef WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #ifdef LEGACY_WIN32
      #include <wspiapi.h>
   #endif
#else
   #include <unistd.h>
#endif
#include <cstring>
#include "api.h"
#include "core.h"

using namespace std;

CUDTSocket::CUDTSocket():
m_Status(INIT),
m_TimeStamp(0),
m_iIPversion(0),
m_pSelfAddr(NULL),
m_pPeerAddr(NULL),
m_SocketID(0),
m_ListenSocket(0),
m_PeerID(0),
m_iISN(0),
m_pUDT(NULL),
m_pQueuedSockets(NULL),
m_pAcceptSockets(NULL),
m_AcceptCond(),
m_AcceptLock(),
m_uiBackLog(0),
m_iMuxID(-1)
{
   #ifndef WIN32
      pthread_mutex_init(&m_AcceptLock, NULL);
      pthread_cond_init(&m_AcceptCond, NULL);
      pthread_mutex_init(&m_ControlLock, NULL);
   #else
      m_AcceptLock = CreateMutex(NULL, false, NULL);
      m_AcceptCond = CreateEvent(NULL, false, false, NULL);
      m_ControlLock = CreateMutex(NULL, false, NULL);
   #endif
}

CUDTSocket::~CUDTSocket()
{
   if (AF_INET == m_iIPversion)
   {
      delete (sockaddr_in*)m_pSelfAddr;
      delete (sockaddr_in*)m_pPeerAddr;
   }
   else
   {
      delete (sockaddr_in6*)m_pSelfAddr;
      delete (sockaddr_in6*)m_pPeerAddr;
   }

   delete m_pUDT;
   m_pUDT = NULL;

   delete m_pQueuedSockets;
   delete m_pAcceptSockets;

   #ifndef WIN32
      pthread_mutex_destroy(&m_AcceptLock);
      pthread_cond_destroy(&m_AcceptCond);
      pthread_mutex_destroy(&m_ControlLock);
   #else
      CloseHandle(m_AcceptLock);
      CloseHandle(m_AcceptCond);
      CloseHandle(m_ControlLock);
   #endif
}

////////////////////////////////////////////////////////////////////////////////

CUDTUnited::CUDTUnited():
m_Sockets(),
m_ControlLock(),
m_IDLock(),
m_SocketID(0),
m_TLSError(),
m_mMultiplexer(),
m_MultiplexerLock(),
m_pCache(NULL),
m_bClosing(false),
m_GCStopLock(),
m_GCStopCond(),
m_InitLock(),
m_iInstanceCount(0),
m_bGCStatus(false),
m_GCThread(),
m_ClosedSockets()
{
   // Socket ID MUST start from a random value
   // Socket ID 从一个随机值开始
   srand((unsigned int)CTimer::getTime());
   m_SocketID = 1 + (int)((1 << 30) * (double(rand()) / RAND_MAX));

   #ifndef WIN32
      pthread_mutex_init(&m_ControlLock, NULL);
      pthread_mutex_init(&m_IDLock, NULL);
      pthread_mutex_init(&m_InitLock, NULL);
   #else
      m_ControlLock = CreateMutex(NULL, false, NULL);
      m_IDLock = CreateMutex(NULL, false, NULL);
      m_InitLock = CreateMutex(NULL, false, NULL);
   #endif

   #ifndef WIN32
      // 每个线程都有自己独立的errno
      pthread_key_create(&m_TLSError, TLSDestroy);
   #else
      m_TLSError = TlsAlloc();
      m_TLSLock = CreateMutex(NULL, false, NULL);
   #endif

   m_pCache = new CCache<CInfoBlock>;
}

CUDTUnited::~CUDTUnited()
{
   #ifndef WIN32
      pthread_mutex_destroy(&m_ControlLock);
      pthread_mutex_destroy(&m_IDLock);
      pthread_mutex_destroy(&m_InitLock);
   #else
      CloseHandle(m_ControlLock);
      CloseHandle(m_IDLock);
      CloseHandle(m_InitLock);
   #endif

   #ifndef WIN32
      pthread_key_delete(m_TLSError);
   #else
      TlsFree(m_TLSError);
      CloseHandle(m_TLSLock);
   #endif

   delete m_pCache;
}

int CUDTUnited::startup()
{
   // lock_guard
   CGuard gcinit(m_InitLock);

   // 只允许有一个udt实例,此处应当使用单例模式来实现
   if (m_iInstanceCount++ > 0){
      std::cout << "An udt instance is already running..." << std::endl;
      return 0;
   }

   // Global initialization code
   #ifdef WIN32
      WORD wVersionRequested;
      WSADATA wsaData;
      wVersionRequested = MAKEWORD(2, 2);

      if (0 != WSAStartup(wVersionRequested, &wsaData))
         throw CUDTException(1, 0,  WSAGetLastError());
   #endif

   //init CTimer::EventLock

   // 如果资源释放线程正在运行，直接退出
   if (m_bGCStatus)
      return true;

   // 资源回收线程运行控制 = false
   m_bClosing = false;
   #ifndef WIN32
      // 创建资源回收线程
      pthread_mutex_init(&m_GCStopLock, NULL);
      pthread_cond_init(&m_GCStopCond, NULL);
      pthread_create(&m_GCThread, NULL, garbageCollect, this);
   #else
      m_GCStopLock = CreateMutex(NULL, false, NULL);
      m_GCStopCond = CreateEvent(NULL, false, false, NULL);
      DWORD ThreadID;
      m_GCThread = CreateThread(NULL, 0, garbageCollect, this, 0, &ThreadID);
   #endif

   m_bGCStatus = true;

   return 0;
}

int CUDTUnited::cleanup()
{
   CGuard gcinit(m_InitLock);

   if (--m_iInstanceCount > 0)
      return 0;

   //destroy CTimer::EventLock

   if (!m_bGCStatus)
      return 0;

   m_bClosing = true;
   #ifndef WIN32
      pthread_cond_signal(&m_GCStopCond);
      pthread_join(m_GCThread, NULL);
      pthread_mutex_destroy(&m_GCStopLock);
      pthread_cond_destroy(&m_GCStopCond);
   #else
      SetEvent(m_GCStopCond);
      WaitForSingleObject(m_GCThread, INFINITE);
      CloseHandle(m_GCThread);
      CloseHandle(m_GCStopLock);
      CloseHandle(m_GCStopCond);
   #endif

   m_bGCStatus = false;

   // Global destruction code
   #ifdef WIN32
      WSACleanup();
   #endif

   return 0;
}

UDTSOCKET CUDTUnited::newSocket(int af, int type)
{
   if ((type != SOCK_STREAM) && (type != SOCK_DGRAM))
      throw CUDTException(5, 3, 0);

   CUDTSocket* ns = NULL;

   try
   {
      // UDT套接字
      ns = new CUDTSocket;
      // UDT句柄，注意：此时才创建了一个CUDT实例
      ns->m_pUDT = new CUDT;
      if (AF_INET == af)
      {
         // IPv4 地址
         ns->m_pSelfAddr = (sockaddr*)(new sockaddr_in);
         ((sockaddr_in*)(ns->m_pSelfAddr))->sin_port = 0;
      }
      else
      {  
         // IPv6 地址
         ns->m_pSelfAddr = (sockaddr*)(new sockaddr_in6);
         ((sockaddr_in6*)(ns->m_pSelfAddr))->sin6_port = 0;
      }
   }
   catch (...)
   {
      delete ns;
      throw CUDTException(3, 2, 0);
   }

   // 保存UDT套接字m_SocketID,是一个随机值
   CGuard::enterCS(m_IDLock);
   ns->m_SocketID = -- m_SocketID;
   CGuard::leaveCS(m_IDLock);

   // UDT套接字状态：INIT
   ns->m_Status = INIT;
   ns->m_ListenSocket = 0;
   
   // UDT句柄赋值
   ns->m_pUDT->m_SocketID = ns->m_SocketID;
   ns->m_pUDT->m_iSockType = (SOCK_STREAM == type) ? UDT_STREAM : UDT_DGRAM;
   ns->m_pUDT->m_iIPversion = ns->m_iIPversion = af;
   ns->m_pUDT->m_pCache = m_pCache;

   // protect the m_Sockets structure.
   // 使用一个map来保存UDT套接字
   CGuard::enterCS(m_ControlLock);
   try
   {
      m_Sockets[ns->m_SocketID] = ns;
   }
   catch (...)
   {
      //failure and rollback
      CGuard::leaveCS(m_ControlLock);
      delete ns;
      ns = NULL;
   }
   CGuard::leaveCS(m_ControlLock);

   if (NULL == ns)
      throw CUDTException(3, 2, 0);

   return ns->m_SocketID;
}

/* 
   建立新的连接
      1. 首先检查是否存在重复连接
      2. 检查待处理连接队列是否已满，已满则拒绝新的连接
      3. 构建新的连接
      4. 绑定到一个UDP通道
      5. 将新的连接添加到待处理连接队列中
*/
int CUDTUnited::newConnection(const UDTSOCKET listen, const sockaddr* peer, CHandShake* hs)
{
   // 新连接
   CUDTSocket* ns = NULL;
   // 监听套接字
   CUDTSocket* ls = locate(listen);

   if (NULL == ls)
      return -1;

   // if this connection has already been processed
   // 避免重复连接
   if (NULL != (ns = locate(peer, hs->m_iID, hs->m_iISN)))
   {
      // 如果旧的连接已经断开，清理之
      if (ns->m_pUDT->m_bBroken)
      {
         // last connection from the "peer" address has been broken
         ns->m_Status = CLOSED;
         ns->m_TimeStamp = CTimer::getTime();

         // 从监听套接字的待处理连接队列和已连接队列中移除
         CGuard::enterCS(ls->m_AcceptLock);
         ls->m_pQueuedSockets->erase(ns->m_SocketID);
         ls->m_pAcceptSockets->erase(ns->m_SocketID);
         CGuard::leaveCS(ls->m_AcceptLock);
      }
      // 旧的连接正常，说明是一个重复的连接请求，封装一个错误报文，后续返回给对端
      else
      {
         // connection already exist, this is a repeated connection request
         // respond with existing HS information

         hs->m_iISN = ns->m_pUDT->m_iISN;
         hs->m_iMSS = ns->m_pUDT->m_iMSS;
         hs->m_iFlightFlagSize = ns->m_pUDT->m_iFlightFlagSize;
         hs->m_iReqType = -1;
         hs->m_iID = ns->m_SocketID;

         return 0;

         //except for this situation a new connection should be started
      }
   }

   // exceeding backlog, refuse the connection request
   // 待处理连接队列已满，拒绝连接请求
   if (ls->m_pQueuedSockets->size() >= ls->m_uiBackLog)
      return -1;

   // 创建新的连接
   try
   {
      ns = new CUDTSocket;
      ns->m_pUDT = new CUDT(*(ls->m_pUDT));
      if (AF_INET == ls->m_iIPversion)
      {
         ns->m_pSelfAddr = (sockaddr*)(new sockaddr_in);
         ((sockaddr_in*)(ns->m_pSelfAddr))->sin_port = 0;
         ns->m_pPeerAddr = (sockaddr*)(new sockaddr_in);
         memcpy(ns->m_pPeerAddr, peer, sizeof(sockaddr_in));
      }
      else
      {
         ns->m_pSelfAddr = (sockaddr*)(new sockaddr_in6);
         ((sockaddr_in6*)(ns->m_pSelfAddr))->sin6_port = 0;
         ns->m_pPeerAddr = (sockaddr*)(new sockaddr_in6);
         memcpy(ns->m_pPeerAddr, peer, sizeof(sockaddr_in6));
      }
   }
   catch (...)
   {
      delete ns;
      return -1;
   }

   CGuard::enterCS(m_IDLock);
   ns->m_SocketID = -- m_SocketID;
   CGuard::leaveCS(m_IDLock);

   ns->m_ListenSocket = listen;
   ns->m_iIPversion = ls->m_iIPversion;
   ns->m_pUDT->m_SocketID = ns->m_SocketID;
   ns->m_PeerID = hs->m_iID;
   ns->m_iISN = hs->m_iISN;

   int error = 0;

   // 绑定到一个UDP通道
   try
   {
      // bind to the same addr of listening socket
      ns->m_pUDT->open();
      // 复用已存在的监听套接字
      updateMux(ns, ls);
      // 向对端回复握手响应报文
      ns->m_pUDT->connect(peer, hs);
   }
   catch (...)
   {
      error = 1;
      goto ERR_ROLLBACK;
   }

   // 更新状态为CONNECTED
   ns->m_Status = CONNECTED;

   // copy address information of local node
   // 获取本地地址
   ns->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr(ns->m_pSelfAddr);
   CIPAddress::pton(ns->m_pSelfAddr, ns->m_pUDT->m_piSelfIP, ns->m_iIPversion);

   // protect the m_Sockets structure.
   CGuard::enterCS(m_ControlLock);
   try
   {
      m_Sockets[ns->m_SocketID] = ns;
      // 记录来自同一个对方所有的连接请求，避免重复连接
      m_PeerRec[(ns->m_PeerID << 30) + ns->m_iISN].insert(ns->m_SocketID);
   }
   catch (...)
   {
      error = 2;
   }
   CGuard::leaveCS(m_ControlLock);

   // 将新连接添加到待处理连接队列中
   CGuard::enterCS(ls->m_AcceptLock);
   try
   {
      ls->m_pQueuedSockets->insert(ns->m_SocketID);
   }
   catch (...)
   {
      error = 3;
   }
   CGuard::leaveCS(ls->m_AcceptLock);

   // acknowledge users waiting for new connections on the listening socket
   // 更新监听套接字的epoll事件为UDT_EPOLL_IN，等待新连接
   m_EPoll.update_events(listen, ls->m_pUDT->m_sPollID, UDT_EPOLL_IN, true);

   // 唤醒一个等待的accept()调用
   CTimer::triggerEvent();

   ERR_ROLLBACK:
   if (error > 0)
   {
      ns->m_pUDT->close();
      ns->m_Status = CLOSED;
      ns->m_TimeStamp = CTimer::getTime();

      return -1;
   }

   // wake up a waiting accept() call
   #ifndef WIN32
      pthread_mutex_lock(&(ls->m_AcceptLock));
      pthread_cond_signal(&(ls->m_AcceptCond));
      pthread_mutex_unlock(&(ls->m_AcceptLock));
   #else
      SetEvent(ls->m_AcceptCond);
   #endif

   return 1;
}

CUDT* CUDTUnited::lookup(const UDTSOCKET u)
{
   // lock_guard
   CGuard cg(m_ControlLock);

   // 从map中查找UDT对象
   map<UDTSOCKET, CUDTSocket*>::iterator i = m_Sockets.find(u);

   if ((i == m_Sockets.end()) || (i->second->m_Status == CLOSED))
      throw CUDTException(5, 4, 0);

   return i->second->m_pUDT;
}

UDTSTATUS CUDTUnited::getStatus(const UDTSOCKET u)
{
   // protects the m_Sockets structure
   CGuard cg(m_ControlLock);

   map<UDTSOCKET, CUDTSocket*>::iterator i = m_Sockets.find(u);

   if (i == m_Sockets.end())
   {
      if (m_ClosedSockets.find(u) != m_ClosedSockets.end())
         return CLOSED;

      return NONEXIST;
   }

   if (i->second->m_pUDT->m_bBroken)
      return BROKEN;

   return i->second->m_Status;   
}

int CUDTUnited::bind(const UDTSOCKET u, const sockaddr* name, int namelen)
{
   // 从map中查找UDT套接字
   CUDTSocket* s = locate(u);
   if (NULL == s)
      throw CUDTException(5, 4, 0);

   // lock_guard
   CGuard cg(s->m_ControlLock);

   // cannot bind a socket more than once
   // 只有处于INIT状态的socket才可以绑定，避免一个UDT套接字被多次绑定
   if (INIT != s->m_Status)
      throw CUDTException(5, 0, 0);

   // check the size of SOCKADDR structure
   // 检查地址结构体的大小，IPv4和IPv6的地址结构体有所区别
   if (AF_INET == s->m_iIPversion)
   {
      if (namelen != sizeof(sockaddr_in))
         throw CUDTException(5, 3, 0);
   }
   else
   {
      if (namelen != sizeof(sockaddr_in6))
         throw CUDTException(5, 3, 0);
   }

   // 开启一个UDT连接
   s->m_pUDT->open();
   // 更新UDP连接复用器，使用一个已建立的UDP连接或创建一个新的UDP连接
   updateMux(s, name);
   // 状态机：INIT -> OPENED
   s->m_Status = OPENED;

   // copy address information of local node
   // 保存本地地址
   s->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr(s->m_pSelfAddr);

   return 0;
}

int CUDTUnited::bind(UDTSOCKET u, UDPSOCKET udpsock)
{
   // 从Map中查找获取UDT套接字
   CUDTSocket* s = locate(u);
   if (NULL == s)
      throw CUDTException(5, 4, 0);

   // lock_guard
   CGuard cg(s->m_ControlLock);

   // cannot bind a socket more than once
   if (INIT != s->m_Status)
      throw CUDTException(5, 0, 0);

   sockaddr_in name4;
   sockaddr_in6 name6;
   sockaddr* name;
   socklen_t namelen;

   // IPv4 or IPv6
   if (AF_INET == s->m_iIPversion)
   {
      // IPv4
      namelen = sizeof(sockaddr_in);
      name = (sockaddr*)&name4;
   }
   else
   {
      // IPv6
      namelen = sizeof(sockaddr_in6);
      name = (sockaddr*)&name6;
   }

   // 调用系统API获取本地地址信息
   if (-1 == ::getsockname(udpsock, name, &namelen))
      throw CUDTException(5, 3);

   // open一个UDT实例
   s->m_pUDT->open();
   // 使用一个已创建的UDP套接字udpsock
   updateMux(s, name, &udpsock);
   // 更新状态为OPENED
   s->m_Status = OPENED;

   // copy address information of local node
   s->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr(s->m_pSelfAddr);

   return 0;
}

// 创建了两个队列：待处理的连接队列和已连接队列
int CUDTUnited::listen(const UDTSOCKET u, int backlog)
{
   std::cout << "CUDTUnited::listen..." << std::endl;

   // 根据socket id查找CUDTSocket实例
   CUDTSocket* s = locate(u);
   if (NULL == s)
      throw CUDTException(5, 4, 0);
   
   // lock_guard
   CGuard cg(s->m_ControlLock);

   // do nothing if the socket is already listening
   // 如果已经处于LISTENING状态，不要重复listen
   if (LISTENING == s->m_Status)
      return 0;

   // a socket can listen only if is in OPENED status
   // 只有处于OPENED状态的socket才可以listen
   if (OPENED != s->m_Status)
      throw CUDTException(5, 5, 0);

   // listen is not supported in rendezvous connection setup
   // 什么是交会连接模式
   if (s->m_pUDT->m_bRendezvous)
      throw CUDTException(5, 7, 0);

   if (backlog <= 0)
      throw CUDTException(5, 3, 0);

   // 套接字上可以排队的最大连接请求数量
   s->m_uiBackLog = backlog;

   try
   {
      // 正在建立连接的sockfd列表
      s->m_pQueuedSockets = new set<UDTSOCKET>;
      // 已经建立连接的sockfd列表
      s->m_pAcceptSockets = new set<UDTSOCKET>;
   }
   catch (...)
   {
      delete s->m_pQueuedSockets;
      delete s->m_pAcceptSockets;
      throw CUDTException(3, 2, 0);
   }

   // 设置一个标志位，表示处于listen模式
   s->m_pUDT->listen();

   // 进入下一状态LISTENING
   s->m_Status = LISTENING;

   return 0;
}

UDTSOCKET CUDTUnited::accept(const UDTSOCKET listen, sockaddr* addr, int* addrlen)
{
   if ((NULL != addr) && (NULL == addrlen))
      throw CUDTException(5, 3, 0);

   // 根据socket id查找CUDTSocket实例，ls == local socket
   CUDTSocket* ls = locate(listen);

   if (ls == NULL)
      throw CUDTException(5, 4, 0);

   // the "listen" socket must be in LISTENING status
   // 只有处于LISTENING状态的socket才可以accept
   if (LISTENING != ls->m_Status)
      throw CUDTException(5, 6, 0);

   // no "accept" in rendezvous connection setup
   // 交会连接模式是个什么鬼
   if (ls->m_pUDT->m_bRendezvous)
      throw CUDTException(5, 7, 0);

   // 套接字初始化
   UDTSOCKET u = CUDT::INVALID_SOCK;
   bool accepted = false;

   // !!only one conection can be set up each time!!
   // 每次只能建立一个连接
#ifndef WIN32
      // 阻塞，直到获得一个连接请求
      while (!accepted)
      {
         // 临界区保护，确保一次只处理一个连接请求
         pthread_mutex_lock(&(ls->m_AcceptLock));

         // 套接字不可用时，将accepted设置为true,通过这种方式退出循环
         if ((LISTENING != ls->m_Status) || ls->m_pUDT->m_bBroken)
         {
            // This socket has been closed.
            accepted = true;
         }
         // 有待处理的连接请求
         else if (ls->m_pQueuedSockets->size() > 0)
         {
            // 将sockfd从m_pQueuedSockets转移到m_pAcceptSockets
            
            // 获取待连接队列中的第一个元素
            u = *(ls->m_pQueuedSockets->begin());
            // 将sockfd添加到已连接队列的队尾
            ls->m_pAcceptSockets->insert(ls->m_pAcceptSockets->end(), u);
            // 从待连接队列中删除该元素
            ls->m_pQueuedSockets->erase(ls->m_pQueuedSockets->begin());
            // 更新标志位
            accepted = true;
         }
         // 同步接收模式　？？？
         else if (!ls->m_pUDT->m_bSynRecving)
         {
            accepted = true;
         }

         // 没有待处理连接时，通过条件变量休眠
         if (!accepted && (LISTENING == ls->m_Status)){
            pthread_cond_wait(&(ls->m_AcceptCond), &(ls->m_AcceptLock));
         }

         // 待处理的套接字队列为空，将监听套接字从epoll中移除，减少资源占用
         if (ls->m_pQueuedSockets->empty())
            m_EPoll.update_events(listen, ls->m_pUDT->m_sPollID, UDT_EPOLL_IN, false);

         pthread_mutex_unlock(&(ls->m_AcceptLock));
      }
#else
      while (!accepted)
      {
         WaitForSingleObject(ls->m_AcceptLock, INFINITE);

         if (ls->m_pQueuedSockets->size() > 0)
         {
            u = *(ls->m_pQueuedSockets->begin());
            ls->m_pAcceptSockets->insert(ls->m_pAcceptSockets->end(), u);
            ls->m_pQueuedSockets->erase(ls->m_pQueuedSockets->begin());

            accepted = true;
         }
         else if (!ls->m_pUDT->m_bSynRecving)
            accepted = true;

         ReleaseMutex(ls->m_AcceptLock);

         if  (!accepted & (LISTENING == ls->m_Status))
            WaitForSingleObject(ls->m_AcceptCond, INFINITE);

         if ((LISTENING != ls->m_Status) || ls->m_pUDT->m_bBroken)
         {
            // Send signal to other threads that are waiting to accept.
            SetEvent(ls->m_AcceptCond);
            accepted = true;
         }

         if (ls->m_pQueuedSockets->empty())
            m_EPoll.update_events(listen, ls->m_pUDT->m_sPollID, UDT_EPOLL_IN, false);
      }
#endif

   // 没有待处理的连接
   if (u == CUDT::INVALID_SOCK)
   {
      // non-blocking receiving, no connection available
      // 非阻塞接收，没有可用的连接
      if (!ls->m_pUDT->m_bSynRecving)
         throw CUDTException(6, 2, 0);

      // listening socket is closed
      throw CUDTException(5, 6, 0);
   }

   // 获取对端地址
   if ((addr != NULL) && (addrlen != NULL))
   {
      // IPv4/IPv6 地址长度不同
      if (AF_INET == locate(u)->m_iIPversion)
         *addrlen = sizeof(sockaddr_in);
      else
         *addrlen = sizeof(sockaddr_in6);

      // copy address information of peer node
      // 对端地址信息
      memcpy(addr, locate(u)->m_pPeerAddr, *addrlen);
   }

   return u;
}

int CUDTUnited::connect(const UDTSOCKET u, const sockaddr* name, int namelen)
{
   // 从map中查找UDT套接字
   CUDTSocket* s = locate(u);
   if (NULL == s)
      throw CUDTException(5, 4, 0);

   // lock_guard
   CGuard cg(s->m_ControlLock);

   // check the size of SOCKADDR structure
   // IPv4/IPv6的地址长度不同
   if (AF_INET == s->m_iIPversion)
   {
      if (namelen != sizeof(sockaddr_in))
         throw CUDTException(5, 3, 0);
   }
   else
   {
      if (namelen != sizeof(sockaddr_in6))
         throw CUDTException(5, 3, 0);
   }

   // a socket can "connect" only if it is in INIT or OPENED status
   // 状态切换 INIT->OPENED->CONNECTING，只有处于INIT状态的socket才可以调用open()
   if (INIT == s->m_Status)
   {
      if (!s->m_pUDT->m_bRendezvous)
      {
         // 核心逻辑，open一个UDT连接
         s->m_pUDT->open();
         updateMux(s);
         s->m_Status = OPENED;
      }
      else
         throw CUDTException(5, 8, 0);
   }
   // 状态切换 OPENED->CONNECTING
   else if (OPENED != s->m_Status)
      throw CUDTException(5, 2, 0);

   // connect_complete() may be called before connect() returns.
   // So we need to update the status before connect() is called,
   // otherwise the status may be overwritten with wrong value (CONNECTED vs. CONNECTING).
   s->m_Status = CONNECTING;
   try
   {
      // 核心逻辑，开始建立连接
      s->m_pUDT->connect(name);
   }
   catch (CUDTException e)
   {
      s->m_Status = OPENED;
      throw e;
   }

   // 记录对端IP地址
   delete s->m_pPeerAddr;
   if (AF_INET == s->m_iIPversion)
   {
      s->m_pPeerAddr = (sockaddr*)(new sockaddr_in);
      memcpy(s->m_pPeerAddr, name, sizeof(sockaddr_in));
   }
   else
   {
      s->m_pPeerAddr = (sockaddr*)(new sockaddr_in6);
      memcpy(s->m_pPeerAddr, name, sizeof(sockaddr_in6));
   }

   return 0;
}

void CUDTUnited::connect_complete(const UDTSOCKET u)
{
   CUDTSocket* s = locate(u);
   if (NULL == s)
      throw CUDTException(5, 4, 0);

   // copy address information of local node
   // the local port must be correctly assigned BEFORE CUDT::connect(),
   // otherwise if connect() fails, the multiplexer cannot be located by garbage collection and will cause leak
   s->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr(s->m_pSelfAddr);
   CIPAddress::pton(s->m_pSelfAddr, s->m_pUDT->m_piSelfIP, s->m_iIPversion);

   s->m_Status = CONNECTED;
}

int CUDTUnited::close(const UDTSOCKET u)
{
   CUDTSocket* s = locate(u);
   if (NULL == s)
      throw CUDTException(5, 4, 0);

   CGuard socket_cg(s->m_ControlLock);

   if (s->m_Status == LISTENING)
   {
      if (s->m_pUDT->m_bBroken)
         return 0;

      s->m_TimeStamp = CTimer::getTime();
      s->m_pUDT->m_bBroken = true;

      // broadcast all "accept" waiting
      #ifndef WIN32
         pthread_mutex_lock(&(s->m_AcceptLock));
         pthread_cond_broadcast(&(s->m_AcceptCond));
         pthread_mutex_unlock(&(s->m_AcceptLock));
      #else
         SetEvent(s->m_AcceptCond);
      #endif

      return 0;
   }

   s->m_pUDT->close();

   // synchronize with garbage collection.
   CGuard manager_cg(m_ControlLock);

   // since "s" is located before m_ControlLock, locate it again in case it became invalid
   map<UDTSOCKET, CUDTSocket*>::iterator i = m_Sockets.find(u);
   if ((i == m_Sockets.end()) || (i->second->m_Status == CLOSED))
      return 0;
   s = i->second;

   s->m_Status = CLOSED;

   // a socket will not be immediated removed when it is closed
   // in order to prevent other methods from accessing invalid address
   // a timer is started and the socket will be removed after approximately 1 second
   s->m_TimeStamp = CTimer::getTime();

   m_Sockets.erase(s->m_SocketID);
   m_ClosedSockets.insert(pair<UDTSOCKET, CUDTSocket*>(s->m_SocketID, s));

   CTimer::triggerEvent();

   return 0;
}

int CUDTUnited::getpeername(const UDTSOCKET u, sockaddr* name, int* namelen)
{
   if (CONNECTED != getStatus(u))
      throw CUDTException(2, 2, 0);

   CUDTSocket* s = locate(u);

   if (NULL == s)
      throw CUDTException(5, 4, 0);

   if (!s->m_pUDT->m_bConnected || s->m_pUDT->m_bBroken)
      throw CUDTException(2, 2, 0);

   if (AF_INET == s->m_iIPversion)
      *namelen = sizeof(sockaddr_in);
   else
      *namelen = sizeof(sockaddr_in6);

   // copy address information of peer node
   memcpy(name, s->m_pPeerAddr, *namelen);

   return 0;
}

int CUDTUnited::getsockname(const UDTSOCKET u, sockaddr* name, int* namelen)
{
   CUDTSocket* s = locate(u);

   if (NULL == s)
      throw CUDTException(5, 4, 0);

   if (s->m_pUDT->m_bBroken)
      throw CUDTException(5, 4, 0);

   if (INIT == s->m_Status)
      throw CUDTException(2, 2, 0);

   if (AF_INET == s->m_iIPversion)
      *namelen = sizeof(sockaddr_in);
   else
      *namelen = sizeof(sockaddr_in6);

   // copy address information of local node
   memcpy(name, s->m_pSelfAddr, *namelen);

   return 0;
}

int CUDTUnited::select(ud_set* readfds, ud_set* writefds, ud_set* exceptfds, const timeval* timeout)
{
   uint64_t entertime = CTimer::getTime();

   uint64_t to;
   if (NULL == timeout)
      to = 0xFFFFFFFFFFFFFFFFULL;
   else
      to = timeout->tv_sec * 1000000 + timeout->tv_usec;

   // initialize results
   int count = 0;
   set<UDTSOCKET> rs, ws, es;

   // retrieve related UDT sockets
   vector<CUDTSocket*> ru, wu, eu;
   CUDTSocket* s;
   if (NULL != readfds)
      for (set<UDTSOCKET>::iterator i1 = readfds->begin(); i1 != readfds->end(); ++ i1)
      {
         if (BROKEN == getStatus(*i1))
         {
            rs.insert(*i1);
            ++ count;
         }
         else if (NULL == (s = locate(*i1)))
            throw CUDTException(5, 4, 0);
         else
            ru.push_back(s);
      }
   if (NULL != writefds)
      for (set<UDTSOCKET>::iterator i2 = writefds->begin(); i2 != writefds->end(); ++ i2)
      {
         if (BROKEN == getStatus(*i2))
         {
            ws.insert(*i2);
            ++ count;
         }
         else if (NULL == (s = locate(*i2)))
            throw CUDTException(5, 4, 0);
         else
            wu.push_back(s);
      }
   if (NULL != exceptfds)
      for (set<UDTSOCKET>::iterator i3 = exceptfds->begin(); i3 != exceptfds->end(); ++ i3)
      {
         if (BROKEN == getStatus(*i3))
         {
            es.insert(*i3);
            ++ count;
         }
         else if (NULL == (s = locate(*i3)))
            throw CUDTException(5, 4, 0);
         else
            eu.push_back(s);
      }

   do
   {
      // query read sockets
      for (vector<CUDTSocket*>::iterator j1 = ru.begin(); j1 != ru.end(); ++ j1)
      {
         s = *j1;

         if ((s->m_pUDT->m_bConnected && (s->m_pUDT->m_pRcvBuffer->getRcvDataSize() > 0) && ((s->m_pUDT->m_iSockType == UDT_STREAM) || (s->m_pUDT->m_pRcvBuffer->getRcvMsgNum() > 0)))
            || (!s->m_pUDT->m_bListening && (s->m_pUDT->m_bBroken || !s->m_pUDT->m_bConnected))
            || (s->m_pUDT->m_bListening && (s->m_pQueuedSockets->size() > 0))
            || (s->m_Status == CLOSED))
         {
            rs.insert(s->m_SocketID);
            ++ count;
         }
      }

      // query write sockets
      for (vector<CUDTSocket*>::iterator j2 = wu.begin(); j2 != wu.end(); ++ j2)
      {
         s = *j2;

         if ((s->m_pUDT->m_bConnected && (s->m_pUDT->m_pSndBuffer->getCurrBufSize() < s->m_pUDT->m_iSndBufSize))
            || s->m_pUDT->m_bBroken || !s->m_pUDT->m_bConnected || (s->m_Status == CLOSED))
         {
            ws.insert(s->m_SocketID);
            ++ count;
         }
      }

      // query exceptions on sockets
      for (vector<CUDTSocket*>::iterator j3 = eu.begin(); j3 != eu.end(); ++ j3)
      {
         // check connection request status, not supported now
      }

      if (0 < count)
         break;

      CTimer::waitForEvent();
   } while (to > CTimer::getTime() - entertime);

   if (NULL != readfds)
      *readfds = rs;

   if (NULL != writefds)
      *writefds = ws;

   if (NULL != exceptfds)
      *exceptfds = es;

   return count;
}

int CUDTUnited::selectEx(const vector<UDTSOCKET>& fds, vector<UDTSOCKET>* readfds, vector<UDTSOCKET>* writefds, vector<UDTSOCKET>* exceptfds, int64_t msTimeOut)
{
   uint64_t entertime = CTimer::getTime();

   uint64_t to;
   if (msTimeOut >= 0)
      to = msTimeOut * 1000;
   else
      to = 0xFFFFFFFFFFFFFFFFULL;

   // initialize results
   int count = 0;
   if (NULL != readfds)
      readfds->clear();
   if (NULL != writefds)
      writefds->clear();
   if (NULL != exceptfds)
      exceptfds->clear();

   do
   {
      for (vector<UDTSOCKET>::const_iterator i = fds.begin(); i != fds.end(); ++ i)
      {
         CUDTSocket* s = locate(*i);

         if ((NULL == s) || s->m_pUDT->m_bBroken || (s->m_Status == CLOSED))
         {
            if (NULL != exceptfds)
            {
               exceptfds->push_back(*i);
               ++ count;
            }
            continue;
         }

         if (NULL != readfds)
         {
            if ((s->m_pUDT->m_bConnected && (s->m_pUDT->m_pRcvBuffer->getRcvDataSize() > 0) && ((s->m_pUDT->m_iSockType == UDT_STREAM) || (s->m_pUDT->m_pRcvBuffer->getRcvMsgNum() > 0)))
               || (s->m_pUDT->m_bListening && (s->m_pQueuedSockets->size() > 0)))
            {
               readfds->push_back(s->m_SocketID);
               ++ count;
            }
         }

         if (NULL != writefds)
         {
            if (s->m_pUDT->m_bConnected && (s->m_pUDT->m_pSndBuffer->getCurrBufSize() < s->m_pUDT->m_iSndBufSize))
            {
               writefds->push_back(s->m_SocketID);
               ++ count;
            }
         }
      }

      if (count > 0)
         break;

      CTimer::waitForEvent();
   } while (to > CTimer::getTime() - entertime);

   return count;
}

int CUDTUnited::epoll_create()
{
   return m_EPoll.create();
}

int CUDTUnited::epoll_add_usock(const int eid, const UDTSOCKET u, const int* events)
{
   CUDTSocket* s = locate(u);
   int ret = -1;
   if (NULL != s)
   {
      ret = m_EPoll.add_usock(eid, u, events);
      s->m_pUDT->addEPoll(eid);
   }
   else
   {
      throw CUDTException(5, 4);
   }

   return ret;
}

int CUDTUnited::epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events)
{
   return m_EPoll.add_ssock(eid, s, events);
}

int CUDTUnited::epoll_remove_usock(const int eid, const UDTSOCKET u)
{
   int ret = m_EPoll.remove_usock(eid, u);

   CUDTSocket* s = locate(u);
   if (NULL != s)
   {
      s->m_pUDT->removeEPoll(eid);
   }
   //else
   //{
   //   throw CUDTException(5, 4);
   //}

   return ret;
}

int CUDTUnited::epoll_remove_ssock(const int eid, const SYSSOCKET s)
{
   return m_EPoll.remove_ssock(eid, s);
}

int CUDTUnited::epoll_wait(const int eid, set<UDTSOCKET>* readfds, set<UDTSOCKET>* writefds, int64_t msTimeOut, set<SYSSOCKET>* lrfds, set<SYSSOCKET>* lwfds)
{
   return m_EPoll.wait(eid, readfds, writefds, msTimeOut, lrfds, lwfds);
}

int CUDTUnited::epoll_release(const int eid)
{
   return m_EPoll.release(eid);
}

// 根据socket id从m_Sockets中查找对应的CUDTSocket实例
CUDTSocket* CUDTUnited::locate(const UDTSOCKET u)
{
   CGuard cg(m_ControlLock);

   map<UDTSOCKET, CUDTSocket*>::iterator i = m_Sockets.find(u);

   if ((i == m_Sockets.end()) || (i->second->m_Status == CLOSED))
      return NULL;

   return i->second;
}

CUDTSocket* CUDTUnited::locate(const sockaddr* peer, const UDTSOCKET id, int32_t isn)
{
   CGuard cg(m_ControlLock);

   map<int64_t, set<UDTSOCKET> >::iterator i = m_PeerRec.find((id << 30) + isn);
   if (i == m_PeerRec.end())
      return NULL;

   for (set<UDTSOCKET>::iterator j = i->second.begin(); j != i->second.end(); ++ j)
   {
      map<UDTSOCKET, CUDTSocket*>::iterator k = m_Sockets.find(*j);
      // this socket might have been closed and moved m_ClosedSockets
      if (k == m_Sockets.end())
         continue;

      if (CIPAddress::ipcmp(peer, k->second->m_pPeerAddr, k->second->m_iIPversion))
         return k->second;
   }

   return NULL;
}

void CUDTUnited::checkBrokenSockets()
{
   CGuard cg(m_ControlLock);

   // set of sockets To Be Closed and To Be Removed
   vector<UDTSOCKET> tbc;     // 将要被关闭的socket
   vector<UDTSOCKET> tbr;     // 将要被删除的socket

   // 检查socket状态
   for (map<UDTSOCKET, CUDTSocket*>::iterator i = m_Sockets.begin(); i != m_Sockets.end(); ++ i)
   {
      // check broken connection
      if (i->second->m_pUDT->m_bBroken)
      {
         if (i->second->m_Status == LISTENING)
         {
            // for a listening socket, it should wait an extra 3 seconds in case a client is connecting
            // listening 的socket,需要额外等待3秒，防止有客户端正在尝试连接，这么做有什么好处?
            if (CTimer::getTime() - i->second->m_TimeStamp < 3000000)
               continue;
         }
         // 如果接受有缓冲区中有数据，continue
         else if ((i->second->m_pUDT->m_pRcvBuffer != NULL) && (i->second->m_pUDT->m_pRcvBuffer->getRcvDataSize() > 0) && (i->second->m_pUDT->m_iBrokenCounter -- > 0))
         {
            // if there is still data in the receiver buffer, wait longer
            continue;
         }

         //close broken connections and start removal timer
         // 将socket设置为CLOSED状态，将其放入待关闭列表tbc中，更新m_ClosedSockets中对应的信息
         i->second->m_Status = CLOSED;
         i->second->m_TimeStamp = CTimer::getTime();
         tbc.push_back(i->first);
         m_ClosedSockets[i->first] = i->second;

         // remove from listener's queue
         // 从m_Sockets和m_ClosedSockets中查找listen sockfd
         map<UDTSOCKET, CUDTSocket*>::iterator ls = m_Sockets.find(i->second->m_ListenSocket);
         if (ls == m_Sockets.end())
         {
            ls = m_ClosedSockets.find(i->second->m_ListenSocket);
            if (ls == m_ClosedSockets.end())
               continue;
         }

         // 清除待连接列表和已连接列表中的sockfd
         CGuard::enterCS(ls->second->m_AcceptLock);
         ls->second->m_pQueuedSockets->erase(i->second->m_SocketID);
         ls->second->m_pAcceptSockets->erase(i->second->m_SocketID);
         CGuard::leaveCS(ls->second->m_AcceptLock);
      }
   }

   // 检查处于closed状态的sockfd
   for (map<UDTSOCKET, CUDTSocket*>::iterator j = m_ClosedSockets.begin(); j != m_ClosedSockets.end(); ++ j)
   {
      // sockfd对应的发送缓冲区中仍有数据，并且尚未超时
      if (j->second->m_pUDT->m_ullLingerExpiration > 0)
      {
         // asynchronous close: 
         // 异步关闭：如果发送缓冲区为空或超时，则关闭
         if ((NULL == j->second->m_pUDT->m_pSndBuffer) || (0 == j->second->m_pUDT->m_pSndBuffer->getCurrBufSize()) || (j->second->m_pUDT->m_ullLingerExpiration <= CTimer::getTime()))
         {
            j->second->m_pUDT->m_ullLingerExpiration = 0;   // 清空超时时间
            j->second->m_pUDT->m_bClosing = true;           // 更新sockfd状态 - closing
            j->second->m_TimeStamp = CTimer::getTime();     // 更新时间戳
         }
      }

      // timeout 1 second to destroy a socket AND it has been removed from RcvUList
      // 等待１秒，等待sockfd销毁和从RcvUList中删除，这个操作有必要吗？使用延时的方式合理吗？
      if ((CTimer::getTime() - j->second->m_TimeStamp > 1000000) && ((NULL == j->second->m_pUDT->m_pRNode) || !j->second->m_pUDT->m_pRNode->m_bOnList))
      {
         // 将其放到待删除列表中
         tbr.push_back(j->first);
      }
   }

   // move closed sockets to the ClosedSockets structure
   for (vector<UDTSOCKET>::iterator k = tbc.begin(); k != tbc.end(); ++ k)
      m_Sockets.erase(*k);

   // remove those timeout sockets
   for (vector<UDTSOCKET>::iterator l = tbr.begin(); l != tbr.end(); ++ l)
      removeSocket(*l);
}

void CUDTUnited::removeSocket(const UDTSOCKET u)
{
   map<UDTSOCKET, CUDTSocket*>::iterator i = m_ClosedSockets.find(u);

   // invalid socket ID
   if (i == m_ClosedSockets.end())
      return;

   // decrease multiplexer reference count, and remove it if necessary
   const int mid = i->second->m_iMuxID;

   if (NULL != i->second->m_pQueuedSockets)
   {
      CGuard::enterCS(i->second->m_AcceptLock);

      // if it is a listener, close all un-accepted sockets in its queue and remove them later
      for (set<UDTSOCKET>::iterator q = i->second->m_pQueuedSockets->begin(); q != i->second->m_pQueuedSockets->end(); ++ q)
      {
         m_Sockets[*q]->m_pUDT->m_bBroken = true;
         m_Sockets[*q]->m_pUDT->close();
         m_Sockets[*q]->m_TimeStamp = CTimer::getTime();
         m_Sockets[*q]->m_Status = CLOSED;
         m_ClosedSockets[*q] = m_Sockets[*q];
         m_Sockets.erase(*q);
      }

      CGuard::leaveCS(i->second->m_AcceptLock);
   }

   // remove from peer rec
   map<int64_t, set<UDTSOCKET> >::iterator j = m_PeerRec.find((i->second->m_PeerID << 30) + i->second->m_iISN);
   if (j != m_PeerRec.end())
   {
      j->second.erase(u);
      if (j->second.empty())
         m_PeerRec.erase(j);
   }

   // delete this one
   i->second->m_pUDT->close();
   delete i->second;
   m_ClosedSockets.erase(i);

   map<int, CMultiplexer>::iterator m;
   m = m_mMultiplexer.find(mid);
   if (m == m_mMultiplexer.end())
   {
      //something is wrong!!!
      return;
   }

   m->second.m_iRefCount --;
   if (0 == m->second.m_iRefCount)
   {
      m->second.m_pChannel->close();
      delete m->second.m_pSndQueue;
      delete m->second.m_pRcvQueue;
      delete m->second.m_pTimer;
      delete m->second.m_pChannel;
      m_mMultiplexer.erase(m);
   }
}

void CUDTUnited::setError(CUDTException* e)
{
   #ifndef WIN32
      delete (CUDTException*)pthread_getspecific(m_TLSError);
      pthread_setspecific(m_TLSError, e);
   #else
      CGuard tg(m_TLSLock);
      delete (CUDTException*)TlsGetValue(m_TLSError);
      TlsSetValue(m_TLSError, e);
      m_mTLSRecord[GetCurrentThreadId()] = e;
   #endif
}

CUDTException* CUDTUnited::getError()
{
   #ifndef WIN32
      if(NULL == pthread_getspecific(m_TLSError))
         pthread_setspecific(m_TLSError, new CUDTException);
      return (CUDTException*)pthread_getspecific(m_TLSError);
   #else
      CGuard tg(m_TLSLock);
      if(NULL == TlsGetValue(m_TLSError))
      {
         CUDTException* e = new CUDTException;
         TlsSetValue(m_TLSError, e);
         m_mTLSRecord[GetCurrentThreadId()] = e;
      }
      return (CUDTException*)TlsGetValue(m_TLSError);
   #endif
}

#ifdef WIN32
void CUDTUnited::checkTLSValue()
{
   CGuard tg(m_TLSLock);

   vector<DWORD> tbr;
   for (map<DWORD, CUDTException*>::iterator i = m_mTLSRecord.begin(); i != m_mTLSRecord.end(); ++ i)
   {
      HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, i->first);
      if (NULL == h)
      {
         tbr.push_back(i->first);
         break;
      }
      if (WAIT_OBJECT_0 == WaitForSingleObject(h, 0))
      {
         delete i->second;
         tbr.push_back(i->first);
      }
      CloseHandle(h);
   }
   for (vector<DWORD>::iterator j = tbr.begin(); j != tbr.end(); ++ j)
      m_mTLSRecord.erase(*j);
}
#endif

/*
   UDP通道复用
      1. 首先从map中查找UDP通道是否已经存在，已存在则复用
      2. 不存在，则创建新的UDP通道，并加入到map中
      3. server端：在UDP的bind阶段调用，加入监听套接字
      4. client端：在UDP的connect阶段调用，加入local套接字
 */
void CUDTUnited::updateMux(CUDTSocket* s, const sockaddr* addr, const UDPSOCKET* udpsock)
{
   // std::cout << "CUDTUnited::updateMux()..." << std::endl;
   CGuard cg(m_ControlLock);

   // 地址复用
   if ((s->m_pUDT->m_bReuseAddr) && (NULL != addr))
   {
      // 从IPv4/IPv6地址中获取端口号
      int port = (AF_INET == s->m_pUDT->m_iIPversion) ? ntohs(((sockaddr_in*)addr)->sin_port) : ntohs(((sockaddr_in6*)addr)->sin6_port);

      // find a reusable address
      // 找到一个可复用的地址，即使用一个已建立的UDP物理通道
      for (map<int, CMultiplexer>::iterator i = m_mMultiplexer.begin(); i != m_mMultiplexer.end(); ++ i)
      {
         if ((i->second.m_iIPversion == s->m_pUDT->m_iIPversion) && (i->second.m_iMSS == s->m_pUDT->m_iMSS) && i->second.m_bReusable)
         {
            if (i->second.m_iPort == port)
            {
               // reuse the existing multiplexer
               // 复用已存在的UDP物理通道
               ++ i->second.m_iRefCount;
               s->m_pUDT->m_pSndQueue = i->second.m_pSndQueue;
               s->m_pUDT->m_pRcvQueue = i->second.m_pRcvQueue;
               s->m_iMuxID = i->second.m_iID;
               return;
            }
         }
      }
   }

   // a new multiplexer is needed
   // 不存在匹配的UDP通道,则创建一个新的UDP通道
   CMultiplexer m;
   m.m_iMSS = s->m_pUDT->m_iMSS;
   m.m_iIPversion = s->m_pUDT->m_iIPversion;
   m.m_iRefCount = 1;
   m.m_bReusable = s->m_pUDT->m_bReuseAddr;
   m.m_iID = s->m_SocketID;

   // 创建UDP通道
   m.m_pChannel = new CChannel(s->m_pUDT->m_iIPversion);
   m.m_pChannel->setSndBufSize(s->m_pUDT->m_iUDPSndBufSize);
   m.m_pChannel->setRcvBufSize(s->m_pUDT->m_iUDPRcvBufSize);

   // 到这一步才真正调用系统socket创建了一个套接字,并进行bind
   try
   {
      // 调用系统API socket/bind
      if (NULL != udpsock)
         m.m_pChannel->open(*udpsock);
      else
         m.m_pChannel->open(addr);
   }
   catch (CUDTException& e)
   {
      // 调用系统API close
      m.m_pChannel->close();
      delete m.m_pChannel;
      throw e;
   }

   // 更新CMultiplexer相关信息
   sockaddr* sa = (AF_INET == s->m_pUDT->m_iIPversion) ? (sockaddr*) new sockaddr_in : (sockaddr*) new sockaddr_in6;
   m.m_pChannel->getSockAddr(sa);
   m.m_iPort = (AF_INET == s->m_pUDT->m_iIPversion) ? ntohs(((sockaddr_in*)sa)->sin_port) : ntohs(((sockaddr_in6*)sa)->sin6_port);
   if (AF_INET == s->m_pUDT->m_iIPversion) delete (sockaddr_in*)sa; else delete (sockaddr_in6*)sa;

   // 创建一个定时器
   m.m_pTimer = new CTimer;

   // 创建发送/接收队列
   m.m_pSndQueue = new CSndQueue;
   m.m_pSndQueue->init(m.m_pChannel, m.m_pTimer);
   m.m_pRcvQueue = new CRcvQueue;
   m.m_pRcvQueue->init(32, s->m_pUDT->m_iPayloadSize, m.m_iIPversion, 1024, m.m_pChannel, m.m_pTimer);

   // 保存CMultiplexer到map中
   m_mMultiplexer[m.m_iID] = m;

   // 更新UDT套接字发送/接收队列
   s->m_pUDT->m_pSndQueue = m.m_pSndQueue;
   s->m_pUDT->m_pRcvQueue = m.m_pRcvQueue;
   s->m_iMuxID = m.m_iID;
}

// 监听套接字复用
void CUDTUnited::updateMux(CUDTSocket* s, const CUDTSocket* ls)
{
   // lock_guard
   CGuard cg(m_ControlLock);

   // 从IPv4/IPv6地址中获取端口号
   int port = (AF_INET == ls->m_iIPversion) ? ntohs(((sockaddr_in*)ls->m_pSelfAddr)->sin_port) : ntohs(((sockaddr_in6*)ls->m_pSelfAddr)->sin6_port);

   // find the listener's address
   // 查找监听套接字
   for (map<int, CMultiplexer>::iterator i = m_mMultiplexer.begin(); i != m_mMultiplexer.end(); ++ i)
   {
      // 存在已建立的UDP通道，则复用之
      if (i->second.m_iPort == port)
      {
         // reuse the existing multiplexer
         ++ i->second.m_iRefCount;   // 引用计数+1
         s->m_pUDT->m_pSndQueue = i->second.m_pSndQueue;
         s->m_pUDT->m_pRcvQueue = i->second.m_pRcvQueue;
         s->m_iMuxID = i->second.m_iID;
         return;
      }
   }
}

#ifndef WIN32
   void* CUDTUnited::garbageCollect(void* p)
#else
   DWORD WINAPI CUDTUnited::garbageCollect(LPVOID p)
#endif
{
   CUDTUnited* self = (CUDTUnited*)p;

   CGuard gcguard(self->m_GCStopLock);

   while (!self->m_bClosing)
   {
      // 检查sockfd状态，将不同状态的sockfd转移到不同的列表中
      self->checkBrokenSockets();

      #ifdef WIN32
         self->checkTLSValue();
      #endif

      #ifndef WIN32
         timeval now;
         timespec timeout;
         gettimeofday(&now, 0);
         timeout.tv_sec = now.tv_sec + 1;          // 1秒的超时时间
         timeout.tv_nsec = now.tv_usec * 1000;

         // 等待　m_GCStopCond　这个条件变量，超时时间为1秒
         pthread_cond_timedwait(&self->m_GCStopCond, &self->m_GCStopLock, &timeout);
      #else
         WaitForSingleObject(self->m_GCStopCond, 1000);
      #endif
   }

   // remove all sockets and multiplexers
   // 回收所有资源
   CGuard::enterCS(self->m_ControlLock);
   for (map<UDTSOCKET, CUDTSocket*>::iterator i = self->m_Sockets.begin(); i != self->m_Sockets.end(); ++ i)
   {
      // 回收普通soskcfd
      i->second->m_pUDT->m_bBroken = true;
      i->second->m_pUDT->close();
      i->second->m_Status = CLOSED;
      i->second->m_TimeStamp = CTimer::getTime();
      self->m_ClosedSockets[i->first] = i->second;

      // remove from listener's queue
      // 回收listen sockfd
      map<UDTSOCKET, CUDTSocket*>::iterator ls = self->m_Sockets.find(i->second->m_ListenSocket);
      if (ls == self->m_Sockets.end())
      {
         ls = self->m_ClosedSockets.find(i->second->m_ListenSocket);
         if (ls == self->m_ClosedSockets.end())
            continue;
      }

      // 回收正在连接或已建立连接的sockfd
      CGuard::enterCS(ls->second->m_AcceptLock);
      ls->second->m_pQueuedSockets->erase(i->second->m_SocketID);
      ls->second->m_pAcceptSockets->erase(i->second->m_SocketID);
      CGuard::leaveCS(ls->second->m_AcceptLock);
   }
   self->m_Sockets.clear();

   // 清空m_ClosedSockets列表中的sockfd时间戳，使之立即被回收
   for (map<UDTSOCKET, CUDTSocket*>::iterator j = self->m_ClosedSockets.begin(); j != self->m_ClosedSockets.end(); ++ j)
   {
      j->second->m_TimeStamp = 0;
   }
   CGuard::leaveCS(self->m_ControlLock);

   while (true)
   {
      // 检查sockfd状态，将不同状态的sockfd转移到不同的列表中
      self->checkBrokenSockets();

      CGuard::enterCS(self->m_ControlLock);
      bool empty = self->m_ClosedSockets.empty();
      CGuard::leaveCS(self->m_ControlLock);

      if (empty)
         break;

      CTimer::sleep();
   }

   #ifndef WIN32
      return NULL;
   #else
      return 0;
   #endif
}

////////////////////////////////////////////////////////////////////////////////

int CUDT::startup()
{
   // 注意：s_UDTUnited是一个静态全局变量，在程序启动时自动构建初始化
   return s_UDTUnited.startup();
}

int CUDT::cleanup()
{
   return s_UDTUnited.cleanup();
}

UDTSOCKET CUDT::socket(int af, int type, int)
{
   // 检查是否处于资源回收阶段
   if (!s_UDTUnited.m_bGCStatus)
      s_UDTUnited.startup();

   try
   {
      // 创建一个sockfd，将其加入到m_Sockets这个map中
      return s_UDTUnited.newSocket(af, type);
   }
   catch (CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return INVALID_SOCK;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(3, 2, 0));
      return INVALID_SOCK;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return INVALID_SOCK;
   }
}

// CUDT的静态成员函数
int CUDT::bind(UDTSOCKET u, const sockaddr* name, int namelen)
{
   try
   {
      return s_UDTUnited.bind(u, name, namelen);
   }
   catch (CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(3, 2, 0));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::bind(UDTSOCKET u, UDPSOCKET udpsock)
{
   try
   {
      return s_UDTUnited.bind(u, udpsock);
   }
   catch (CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(3, 2, 0));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::listen(UDTSOCKET u, int backlog)
{
   try
   {
      return s_UDTUnited.listen(u, backlog);
   }
   catch (CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(3, 2, 0));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

UDTSOCKET CUDT::accept(UDTSOCKET u, sockaddr* addr, int* addrlen)
{
   try
   {
      return s_UDTUnited.accept(u, addr, addrlen);
   }
   catch (CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return INVALID_SOCK;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return INVALID_SOCK;
   }
}

int CUDT::connect(UDTSOCKET u, const sockaddr* name, int namelen)
{
   try
   {
      return s_UDTUnited.connect(u, name, namelen);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(3, 2, 0));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::close(UDTSOCKET u)
{
   try
   {
      return s_UDTUnited.close(u);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::getpeername(UDTSOCKET u, sockaddr* name, int* namelen)
{
   try
   {
      return s_UDTUnited.getpeername(u, name, namelen);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::getsockname(UDTSOCKET u, sockaddr* name, int* namelen)
{
   try
   {
      return s_UDTUnited.getsockname(u, name, namelen);;
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::getsockopt(UDTSOCKET u, int, UDTOpt optname, void* optval, int* optlen)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      udt->getOpt(optname, optval, *optlen);
      return 0;
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::setsockopt(UDTSOCKET u, int, UDTOpt optname, const void* optval, int optlen)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      udt->setOpt(optname, optval, optlen);
      return 0;
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::send(UDTSOCKET u, const char* buf, int len, int)
{
   try
   {
      // 从map中找到udt实例
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->send(buf, len);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(3, 2, 0));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::recv(UDTSOCKET u, char* buf, int len, int)
{
   try
   {
      // 查找UDT实例
      CUDT* udt = s_UDTUnited.lookup(u);
      // 接收数据
      return udt->recv(buf, len);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::sendmsg(UDTSOCKET u, const char* buf, int len, int ttl, bool inorder)
{
   try
   {
      // 查找UDT实例
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->sendmsg(buf, len, ttl, inorder);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(3, 2, 0));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::recvmsg(UDTSOCKET u, char* buf, int len)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->recvmsg(buf, len);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int64_t CUDT::sendfile(UDTSOCKET u, fstream& ifs, int64_t& offset, int64_t size, int block)
{
   try
   {
      // 从map中查找udt实例
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->sendfile(ifs, offset, size, block);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(3, 2, 0));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int64_t CUDT::recvfile(UDTSOCKET u, fstream& ofs, int64_t& offset, int64_t size, int block)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->recvfile(ofs, offset, size, block);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::select(int, ud_set* readfds, ud_set* writefds, ud_set* exceptfds, const timeval* timeout)
{
   if ((NULL == readfds) && (NULL == writefds) && (NULL == exceptfds))
   {
      s_UDTUnited.setError(new CUDTException(5, 3, 0));
      return ERROR;
   }

   try
   {
      return s_UDTUnited.select(readfds, writefds, exceptfds, timeout);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(3, 2, 0));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::selectEx(const vector<UDTSOCKET>& fds, vector<UDTSOCKET>* readfds, vector<UDTSOCKET>* writefds, vector<UDTSOCKET>* exceptfds, int64_t msTimeOut)
{
   if ((NULL == readfds) && (NULL == writefds) && (NULL == exceptfds))
   {
      s_UDTUnited.setError(new CUDTException(5, 3, 0));
      return ERROR;
   }

   try
   {
      return s_UDTUnited.selectEx(fds, readfds, writefds, exceptfds, msTimeOut);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(3, 2, 0));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::epoll_create()
{
   try
   {
      return s_UDTUnited.epoll_create();
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::epoll_add_usock(const int eid, const UDTSOCKET u, const int* events)
{
   try
   {
      return s_UDTUnited.epoll_add_usock(eid, u, events);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events)
{
   try
   {
      return s_UDTUnited.epoll_add_ssock(eid, s, events);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::epoll_remove_usock(const int eid, const UDTSOCKET u)
{
   try
   {
      return s_UDTUnited.epoll_remove_usock(eid, u);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::epoll_remove_ssock(const int eid, const SYSSOCKET s)
{
   try
   {
      return s_UDTUnited.epoll_remove_ssock(eid, s);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::epoll_wait(const int eid, set<UDTSOCKET>* readfds, set<UDTSOCKET>* writefds, int64_t msTimeOut, set<SYSSOCKET>* lrfds, set<SYSSOCKET>* lwfds)
{
   try
   {
      return s_UDTUnited.epoll_wait(eid, readfds, writefds, msTimeOut, lrfds, lwfds);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

int CUDT::epoll_release(const int eid)
{
   try
   {
      return s_UDTUnited.epoll_release(eid);
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

CUDTException& CUDT::getlasterror()
{
   return *s_UDTUnited.getError();
}

int CUDT::perfmon(UDTSOCKET u, CPerfMon* perf, bool clear)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      udt->sample(perf, clear);
      return 0;
   }
   catch (CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return ERROR;
   }
}

CUDT* CUDT::getUDTHandle(UDTSOCKET u)
{
   try
   {
      return s_UDTUnited.lookup(u);
   }
   catch (...)
   {
      return NULL;
   }
}

UDTSTATUS CUDT::getsockstate(UDTSOCKET u)
{
   try
   {
      return s_UDTUnited.getStatus(u);
   }
   catch (...)
   {
      s_UDTUnited.setError(new CUDTException(-1, 0, 0));
      return NONEXIST;
   }
}


////////////////////////////////////////////////////////////////////////////////

namespace UDT
{

int startup()
{
   return CUDT::startup();
}

int cleanup()
{
   return CUDT::cleanup();
}

UDTSOCKET socket(int af, int type, int protocol)
{
   return CUDT::socket(af, type, protocol);
}

int bind(UDTSOCKET u, const struct sockaddr* name, int namelen)
{
   return CUDT::bind(u, name, namelen);
}

int bind2(UDTSOCKET u, UDPSOCKET udpsock)
{
   return CUDT::bind(u, udpsock);
}

int listen(UDTSOCKET u, int backlog)
{
   return CUDT::listen(u, backlog);
}

UDTSOCKET accept(UDTSOCKET u, struct sockaddr* addr, int* addrlen)
{
   return CUDT::accept(u, addr, addrlen);
}

int connect(UDTSOCKET u, const struct sockaddr* name, int namelen)
{
   return CUDT::connect(u, name, namelen);
}

int close(UDTSOCKET u)
{
   return CUDT::close(u);
}

int getpeername(UDTSOCKET u, struct sockaddr* name, int* namelen)
{
   return CUDT::getpeername(u, name, namelen);
}

int getsockname(UDTSOCKET u, struct sockaddr* name, int* namelen)
{
   return CUDT::getsockname(u, name, namelen);
}

int getsockopt(UDTSOCKET u, int level, SOCKOPT optname, void* optval, int* optlen)
{
   return CUDT::getsockopt(u, level, optname, optval, optlen);
}

int setsockopt(UDTSOCKET u, int level, SOCKOPT optname, const void* optval, int optlen)
{
   return CUDT::setsockopt(u, level, optname, optval, optlen);
}

int send(UDTSOCKET u, const char* buf, int len, int flags)
{
   return CUDT::send(u, buf, len, flags);
}

int recv(UDTSOCKET u, char* buf, int len, int flags)
{
   return CUDT::recv(u, buf, len, flags);
}

int sendmsg(UDTSOCKET u, const char* buf, int len, int ttl, bool inorder)
{
   return CUDT::sendmsg(u, buf, len, ttl, inorder);
}

int recvmsg(UDTSOCKET u, char* buf, int len)
{
   return CUDT::recvmsg(u, buf, len);
}

int64_t sendfile(UDTSOCKET u, fstream& ifs, int64_t& offset, int64_t size, int block)
{
   return CUDT::sendfile(u, ifs, offset, size, block);
}

int64_t recvfile(UDTSOCKET u, fstream& ofs, int64_t& offset, int64_t size, int block)
{
   return CUDT::recvfile(u, ofs, offset, size, block);
}

int64_t sendfile2(UDTSOCKET u, const char* path, int64_t* offset, int64_t size, int block)
{
   fstream ifs(path, ios::binary | ios::in);
   int64_t ret = CUDT::sendfile(u, ifs, *offset, size, block);
   ifs.close();
   return ret;
}

int64_t recvfile2(UDTSOCKET u, const char* path, int64_t* offset, int64_t size, int block)
{
   fstream ofs(path, ios::binary | ios::out);
   int64_t ret = CUDT::recvfile(u, ofs, *offset, size, block);
   ofs.close();
   return ret;
}

int select(int nfds, UDSET* readfds, UDSET* writefds, UDSET* exceptfds, const struct timeval* timeout)
{
   return CUDT::select(nfds, readfds, writefds, exceptfds, timeout);
}

int selectEx(const vector<UDTSOCKET>& fds, vector<UDTSOCKET>* readfds, vector<UDTSOCKET>* writefds, vector<UDTSOCKET>* exceptfds, int64_t msTimeOut)
{
   return CUDT::selectEx(fds, readfds, writefds, exceptfds, msTimeOut);
}

int epoll_create()
{
   return CUDT::epoll_create();
}

int epoll_add_usock(int eid, UDTSOCKET u, const int* events)
{
   return CUDT::epoll_add_usock(eid, u, events);
}

int epoll_add_ssock(int eid, SYSSOCKET s, const int* events)
{
   return CUDT::epoll_add_ssock(eid, s, events);
}

int epoll_remove_usock(int eid, UDTSOCKET u)
{
   return CUDT::epoll_remove_usock(eid, u);
}

int epoll_remove_ssock(int eid, SYSSOCKET s)
{
   return CUDT::epoll_remove_ssock(eid, s);
}

int epoll_wait(int eid, set<UDTSOCKET>* readfds, set<UDTSOCKET>* writefds, int64_t msTimeOut, set<SYSSOCKET>* lrfds, set<SYSSOCKET>* lwfds)
{
   return CUDT::epoll_wait(eid, readfds, writefds, msTimeOut, lrfds, lwfds);
}

#define SET_RESULT(val, num, fds, it) \
   if ((val != NULL) && !val->empty()) \
   { \
      if (*num > static_cast<int>(val->size())) \
         *num = val->size(); \
      int count = 0; \
      for (it = val->begin(); it != val->end(); ++ it) \
      { \
         if (count >= *num) \
            break; \
         fds[count ++] = *it; \
      } \
   }
int epoll_wait2(int eid, UDTSOCKET* readfds, int* rnum, UDTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum)
{
   // This API is an alternative format for epoll_wait, created for compatability with other languages.
   // Users need to pass in an array for holding the returned sockets, with the maximum array length
   // stored in *rnum, etc., which will be updated with returned number of sockets.

   set<UDTSOCKET> readset;
   set<UDTSOCKET> writeset;
   set<SYSSOCKET> lrset;
   set<SYSSOCKET> lwset;
   set<UDTSOCKET>* rval = NULL;
   set<UDTSOCKET>* wval = NULL;
   set<SYSSOCKET>* lrval = NULL;
   set<SYSSOCKET>* lwval = NULL;
   if ((readfds != NULL) && (rnum != NULL))
      rval = &readset;
   if ((writefds != NULL) && (wnum != NULL))
      wval = &writeset;
   if ((lrfds != NULL) && (lrnum != NULL))
      lrval = &lrset;
   if ((lwfds != NULL) && (lwnum != NULL))
      lwval = &lwset;

   int ret = CUDT::epoll_wait(eid, rval, wval, msTimeOut, lrval, lwval);
   if (ret > 0)
   {
      set<UDTSOCKET>::const_iterator i;
      SET_RESULT(rval, rnum, readfds, i);
      SET_RESULT(wval, wnum, writefds, i);
      set<SYSSOCKET>::const_iterator j;
      SET_RESULT(lrval, lrnum, lrfds, j);
      SET_RESULT(lwval, lwnum, lwfds, j);
   }
   return ret;
}

int epoll_release(int eid)
{
   return CUDT::epoll_release(eid);
}

ERRORINFO& getlasterror()
{
   return CUDT::getlasterror();
}

int getlasterror_code()
{
   return CUDT::getlasterror().getErrorCode();
}

const char* getlasterror_desc()
{
   return CUDT::getlasterror().getErrorMessage();
}

int perfmon(UDTSOCKET u, TRACEINFO* perf, bool clear)
{
   return CUDT::perfmon(u, perf, clear);
}

UDTSTATUS getsockstate(UDTSOCKET u)
{
   return CUDT::getsockstate(u);
}

}  // namespace UDT
