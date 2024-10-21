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
   Yunhong Gu, last updated 09/28/2010
*****************************************************************************/

#ifndef __UDT_API_H__
#define __UDT_API_H__


#include <map>
#include <vector>
#include <iostream>
#include "udt.h"
#include "packet.h"
#include "queue.h"
#include "cache.h"
#include "epoll.h"

class CUDT;


class CUDTSocket
{
public:
   CUDTSocket();
   ~CUDTSocket();

   // 套接字状态
   UDTSTATUS m_Status;                       // current socket state

   // 关闭sockfd时,资源回收使用的计时器
   uint64_t m_TimeStamp;                     // time when the socket is closed

   // IPV4 or IPV6
   int m_iIPversion;                         // IP version
   // local地址
   sockaddr* m_pSelfAddr;                    // pointer to the local address of the socket
   // 对端地址
   sockaddr* m_pPeerAddr;                    // pointer to the peer address of the socket

   // 一个随机值，在CUDTUnited构造函数中生成,用来标识不同的套接字，为啥不直接作用sockfd的值作为标识呢？sockfd不也是唯一的吗？
   UDTSOCKET m_SocketID;                     // socket ID
   // listen sockfd
   UDTSOCKET m_ListenSocket;                 // ID of the listener socket; 0 means this is an independent socket

   // 对端的sockfd
   UDTSOCKET m_PeerID;                       // peer socket ID
   // 初始序列号，用于区分统一端口的不同连接
   int32_t m_iISN;                           // initial sequence number, used to tell different connection from same IP:port

   // CUDT句柄
   CUDT* m_pUDT;                             // pointer to the UDT entity

   // 待处理的sockfd队列
   std::set<UDTSOCKET>* m_pQueuedSockets;    // set of connections waiting for accept()
   // 已建立连接的sockfd队列
   std::set<UDTSOCKET>* m_pAcceptSockets;    // set of accept()ed connections

   // accept阶段使用的条件变量和锁
   pthread_cond_t m_AcceptCond;              // used to block "accept" call
   // 建立UDT连接过程中使用的锁，确保每次只能建立一个连接
   pthread_mutex_t m_AcceptLock;             // mutex associated to m_AcceptCond

   // 套接字上可以排队的最大连接请求数量，listen函数的第二个参数
   unsigned int m_uiBackLog;                 // maximum number of connections in queue

   int m_iMuxID;                             // multiplexer ID

   pthread_mutex_t m_ControlLock;            // lock this socket exclusively for control APIs: bind/listen/connect

private:
   CUDTSocket(const CUDTSocket&);
   CUDTSocket& operator=(const CUDTSocket&); // 仅声明未实现，表示禁用拷贝复制和赋值操作
};

////////////////////////////////////////////////////////////////////////////////

// 负责libudt的　初始化、清理、连接管理、状态统计等工作
class CUDTUnited
{
// 两个友元类
friend class CUDT;
friend class CRendezvousQueue;

public:
   CUDTUnited();
   ~CUDTUnited();

public:

      // Functionality:
      //    initialize the UDT library.
      // Parameters:
      //    None.
      // Returned value:
      //    0 if success, otherwise -1 is returned.

   // 初始化
   int startup();

      // Functionality:
      //    release the UDT library.
      // Parameters:
      //    None.
      // Returned value:
      //    0 if success, otherwise -1 is returned.

   // 清理
   int cleanup();

      // Functionality:
      //    Create a new UDT socket.
      // Parameters:
      //    0) [in] af: IP version, IPv4 (AF_INET) or IPv6 (AF_INET6).
      //    1) [in] type: socket type, SOCK_STREAM or SOCK_DGRAM
      // Returned value:
      //    The new UDT socket ID, or INVALID_SOCK.

   // 创建一个新socket
   UDTSOCKET newSocket(int af, int type);

      // Functionality:
      //    Create a new UDT connection.
      // Parameters:
      //    0) [in] listen: the listening UDT socket;
      //    1) [in] peer: peer address.
      //    2) [in/out] hs: handshake information from peer side (in), negotiated value (out);
      // Returned value:
      //    If the new connection is successfully created: 1 success, 0 already exist, -1 error.

   // 新建一个连接
   int newConnection(const UDTSOCKET listen, const sockaddr* peer, CHandShake* hs);

      // Functionality:
      //    look up the UDT entity according to its ID.
      // Parameters:
      //    0) [in] u: the UDT socket ID.
      // Returned value:
      //    Pointer to the UDT entity.

   // 根据socket来查找UDT实例
   CUDT* lookup(const UDTSOCKET u);

      // Functionality:
      //    Check the status of the UDT socket.
      // Parameters:
      //    0) [in] u: the UDT socket ID.
      // Returned value:
      //    UDT socket status, or NONEXIST if not found.

   // UDTt套接字状态
   UDTSTATUS getStatus(const UDTSOCKET u);

      // socket APIs

   // 服用一个已有的UDP套接字或创建一个新的UDP套接字
   int bind(const UDTSOCKET u, const sockaddr* name, int namelen);
   int bind(const UDTSOCKET u, UDPSOCKET udpsock);
   // 创建了两个队列：待处理的连接队列和已连接队列
   int listen(const UDTSOCKET u, int backlog);
   UDTSOCKET accept(const UDTSOCKET listen, sockaddr* addr, int* addrlen);
   int connect(const UDTSOCKET u, const sockaddr* name, int namelen);
   int close(const UDTSOCKET u);
   int getpeername(const UDTSOCKET u, sockaddr* name, int* namelen);
   int getsockname(const UDTSOCKET u, sockaddr* name, int* namelen);
   int select(ud_set* readfds, ud_set* writefds, ud_set* exceptfds, const timeval* timeout);
   int selectEx(const std::vector<UDTSOCKET>& fds, std::vector<UDTSOCKET>* readfds, std::vector<UDTSOCKET>* writefds, std::vector<UDTSOCKET>* exceptfds, int64_t msTimeOut);
   int epoll_create();
   int epoll_add_usock(const int eid, const UDTSOCKET u, const int* events = NULL);
   int epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
   int epoll_remove_usock(const int eid, const UDTSOCKET u);
   int epoll_remove_ssock(const int eid, const SYSSOCKET s);
   int epoll_wait(const int eid, std::set<UDTSOCKET>* readfds, std::set<UDTSOCKET>* writefds, int64_t msTimeOut, std::set<SYSSOCKET>* lrfds = NULL, std::set<SYSSOCKET>* lwfds = NULL);
   int epoll_release(const int eid);

      // Functionality:
      //    record the UDT exception.
      // Parameters:
      //    0) [in] e: pointer to a UDT exception instance.
      // Returned value:
      //    None.

   // 记录异常
   void setError(CUDTException* e);

      // Functionality:
      //    look up the most recent UDT exception.
      // Parameters:
      //    None.
      // Returned value:
      //    pointer to a UDT exception instance.

   // 获取最新的异常
   CUDTException* getError();

private:
//   void init();

private:
   // 管理所有的UDT socket
   std::map<UDTSOCKET, CUDTSocket*> m_Sockets;       // stores all the socket structures
   // 用来保护m_Sockets
   pthread_mutex_t m_ControlLock;                    // used to synchronize UDT API

   pthread_mutex_t m_IDLock;                         // used to synchronize ID generation
   // 一个随机值
   UDTSOCKET m_SocketID;                             // seed to generate a new unique socket ID

   // 记录来自同一个对方所有的连接请求，避免重复连接
   std::map<int64_t, std::set<UDTSOCKET> > m_PeerRec;// record sockets from peers to avoid repeated connection request, int64_t = (socker_id << 30) + isn

private:
   pthread_key_t m_TLSError;                         // thread local error record (last error)，TLS=Thread Local Storage
   #ifndef WIN32
      static void TLSDestroy(void* e) {if (NULL != e) delete (CUDTException*)e;} // 线程退出时销毁线程特定数据
   #else
      std::map<DWORD, CUDTException*> m_mTLSRecord;
      void checkTLSValue();
      pthread_mutex_t m_TLSLock;
   #endif

private:
   void connect_complete(const UDTSOCKET u);
   // 根据socket id从m_Sockets中查找对应的CUDTSocket实例
   CUDTSocket* locate(const UDTSOCKET u);
   CUDTSocket* locate(const sockaddr* peer, const UDTSOCKET id, int32_t isn);
   // 更新CMultiplexer，每一个CMultiplexer都是一个已建立的UDP连接
   void updateMux(CUDTSocket* s, const sockaddr* addr = NULL, const UDPSOCKET* = NULL);
   void updateMux(CUDTSocket* s, const CUDTSocket* ls);

private:
   // 多路复用器map
   std::map<int, CMultiplexer> m_mMultiplexer;		// UDP multiplexer
   pthread_mutex_t m_MultiplexerLock;

private:
   CCache<CInfoBlock>* m_pCache;			// UDT network information cache

private:
   // 资源释放线程运行控制
   volatile bool m_bClosing;
   // 在资源释放阶段使用的互斥锁
   pthread_mutex_t m_GCStopLock;
   // 在资源释放阶段使用的条件变量
   pthread_cond_t m_GCStopCond;

   // 初始化及释放阶段使用的互斥锁
   pthread_mutex_t m_InitLock;      
   // 只允许有一个实例
   int m_iInstanceCount;				// number of startup() called by application
   // 资源释放线程是否正在运行
   bool m_bGCStatus;					// if the GC thread is working (true)

   pthread_t m_GCThread;
   #ifndef WIN32
      static void* garbageCollect(void*);
   #else
      static DWORD WINAPI garbageCollect(LPVOID);
   #endif

   // 处于closed状态的sockfd
   std::map<UDTSOCKET, CUDTSocket*> m_ClosedSockets;   // temporarily store closed sockets

   // 检查套接字状态
   void checkBrokenSockets();
   // 删除套接字
   void removeSocket(const UDTSOCKET u);

private:
   CEPoll m_EPoll;                                     // handling epoll data structures and events

private:
   CUDTUnited(const CUDTUnited&);
   CUDTUnited& operator=(const CUDTUnited&);
};

#endif
