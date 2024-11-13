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
   Yunhong Gu, last updated 01/27/2011
*****************************************************************************/

#ifndef __UDT_CACHE_H__
#define __UDT_CACHE_H__

#include <list>
#include <vector>

#include "common.h"
#include "udt.h"

// 抽象类，用来表示缓存项
class CCacheItem
{
public:
   // 虚析构
   virtual ~CCacheItem() {}

public:
   // 纯虚函数，用来实现赋值操作
   virtual CCacheItem& operator=(const CCacheItem&) = 0;

   // The "==" operator SHOULD only compare key values.
   // 重载 "==" 运算符
   virtual bool operator==(const CCacheItem&) = 0;

      // Functionality:
      //    get a deep copy clone of the current item
      // Parameters:
      //    None.
      // Returned value:
      //    Pointer to the new item, or NULL if failed.

   // 深拷贝克隆数据
   virtual CCacheItem* clone() = 0;

      // Functionality:
      //    get a random key value between 0 and MAX_INT to be used for the hash in cache
      // Parameters:
      //    None.
      // Returned value:
      //    A random hash key.

   // 缓存hash数据结构中的key
   virtual int getKey() = 0;

   // If there is any shared resources between the cache item and its clone,
   // the shared resource should be released by this function.
   // 如果缓存项与其克隆之间存在任何共享资源，则共享的资源应该被这个函数释放
   virtual void release() {}
};

// 缓存模板类，用于存储和管理缓存项
// 使用了LRU策略,即最近最少使用:主要用于管理缓存中的数据。
// LRU基本思想是：如果一个数据在最近被使用过，那么它在将来被使用的概率也较高；反之，如果一个数据在很长时间内没有被使用过，那么它在将来被使用的概率较低
template<typename T> class CCache
{
public:
   CCache(int size = 1024):
   m_iMaxSize(size),
   m_iHashSize(size * 3),
   m_iCurrSize(0)
   {
      // 哈希表的大小为缓存项个数的三倍，以减少hash冲突
      m_vHashPtr.resize(m_iHashSize);
      // 初始化锁
      CGuard::createMutex(m_Lock);
   }

   ~CCache()
   {
      // 释放所有缓存项及哈希表
      clear();
      // 销毁锁
      CGuard::releaseMutex(m_Lock);
   }

public:
      // Functionality:
      //    find the matching item in the cache.
      // Parameters:
      //    0) [in/out] data: storage for the retrieved item; initially it must carry the key information
      // Returned value:
      //    0 if found a match, otherwise -1.

   // 从缓存中查找匹配项，拷贝数据到data中
   int lookup(T* data)
   {
      // lock_guard
      CGuard cacheguard(m_Lock);

      // 获取key值,要求模板参数T必须实现getKey()函数
      int key = data->getKey();
      if (key < 0)
         return -1;
      // 避免key值超出哈希表的大小
      if (key >= m_iMaxSize)
         key %= m_iHashSize;

      // 哈希查找，找到匹配的list
      const ItemPtrList& item_list = m_vHashPtr[key];
      for (typename ItemPtrList::const_iterator i = item_list.begin(); i != item_list.end(); ++ i)
      {
         // 虽然*data == ***i，但是并不意味者这两个对象完全相同，要看==运算符重载是如何实现的
         if (*data == ***i)
         {
            // copy the cached info
            // 将整个对象进行拷贝
            *data = ***i;
            return 0;
         }
      }

      return -1;
   }

      // Functionality:
      //    update an item in the cache, or insert one if it doesn't exist; oldest item may be removed
      // Parameters:
      //    0) [in] data: the new item to updated/inserted to the cache
      // Returned value:
      //    0 if success, otherwise -1.

   // 更新缓存中的项，或者插入一个不存在的项；最旧的项目可能会被删除
   int update(T* data)
   {
      // RAII方式加锁，函数结束时自动解锁
      CGuard cacheguard(m_Lock);

      // 获取key值，要求模板参数中必须实现了getKey()函数
      int key = data->getKey();
      if (key < 0)
         return -1;
      // 避免key值超出哈希表的大小
      if (key >= m_iMaxSize)
         key %= m_iHashSize;

      // 用于临时存储当前正在处理的项
      T* curr = NULL;

      // 查找现有项，获取key对应的list
      ItemPtrList& item_list = m_vHashPtr[key];
      for (typename ItemPtrList::iterator i = item_list.begin(); i != item_list.end(); ++ i)
      {
         // 如果key对应的数据已存在，则删除之，并将新数据插入到最前面，LRU策略
         if (*data == ***i)
         {
            // update the existing entry with the new value
            // 更新数据内容
            ***i = *data;
            // 保存数据
            curr = **i;

            // remove the current entry
            // 从原位置删除数据
            m_StorageList.erase(*i);
            // 从哈希表中删除数据
            item_list.erase(i);

            // re-insert to the front
            // 将数据重新插入到链表的最前面(LRU策略)
            m_StorageList.push_front(curr);
            // 更新哈希表中的引用
            item_list.push_front(m_StorageList.begin());

            return 0;
         }
      }

      // create new entry and insert to front
      // 如果是新的缓存项，创建并插入到链表的最前面
      curr = data->clone();
      m_StorageList.push_front(curr);
      // 更新哈希表中的引用
      item_list.push_front(m_StorageList.begin());

      // 更新缓存项个数
      ++ m_iCurrSize;
      // 缓冲容量超出限制，则删除最旧的数据
      if (m_iCurrSize >= m_iMaxSize)
      {
         // Cache overflow, remove oldest entry.
         // 缓存溢出，删除最旧的数据
         T* last_data = m_StorageList.back();
         int last_key = last_data->getKey() % m_iHashSize;
         // 从哈希表中删除
         item_list = m_vHashPtr[last_key];
         for (typename ItemPtrList::iterator i = item_list.begin(); i != item_list.end(); ++ i)
         {
            if (*last_data == ***i)
            {
               item_list.erase(i);
               break;
            }
         }
         // 删除数据，释放内存
         last_data->release();
         delete last_data;
         // 删除链表中的数据
         m_StorageList.pop_back();
         // 更新缓存项个数
         -- m_iCurrSize;
      }

      return 0;
   }

      // Functionality:
      //    Specify the cache size (i.e., max number of items).
      // Parameters:
      //    0) [in] size: max cache size.
      // Returned value:
      //    None.

   // 指定缓存大小
   void setSizeLimit(int size)
   {
      // 缓存项个数
      m_iMaxSize = size;
      // 哈希表的大小为缓存项个数的三倍，以减少hash冲突
      m_iHashSize = size * 3;
      // 重新调整哈希表的大小
      m_vHashPtr.resize(m_iHashSize);
   }

      // Functionality:
      //    Clear all entries in the cache, restore to initialization state.
      // Parameters:
      //    None.
      // Returned value:
      //    None.

   // 清除缓存中的所有条目，恢复到初始化状态
   void clear()
   {
      // 释放所有缓存项
      for (typename std::list<T*>::iterator i = m_StorageList.begin(); i != m_StorageList.end(); ++ i)
      {
         (*i)->release();
         delete *i;
      }
      // 清除真正存储缓存项的链表
      m_StorageList.clear();
      // 清除哈希表
      for (typename std::vector<ItemPtrList>::iterator i = m_vHashPtr.begin(); i != m_vHashPtr.end(); ++ i)
         i->clear();

      // 更新缓存项个数
      m_iCurrSize = 0;
   }

private:
   // 真正存储缓存项的链表
   std::list<T*> m_StorageList;
   typedef typename std::list<T*>::iterator ItemPtr;
   typedef std::list<ItemPtr> ItemPtrList;
   // 哈希表中存放的是每个list的入口地址，用于快速找到对应的list
   std::vector<ItemPtrList> m_vHashPtr;

   // 缓存最大大小
   int m_iMaxSize;
   // 哈希表，是m_iMaxSize的三倍，以减少哈希冲突的概率
   int m_iHashSize;
   // 缓存的最大大小
   int m_iCurrSize;

   pthread_mutex_t m_Lock;

private:
   // 仅声明未实现，表示禁用拷贝构造函数
   CCache(const CCache&);
   // 仅声明未实现，表示禁用拷贝赋值
   CCache& operator=(const CCache&);
};

// UDT用于缓存网络连接信息的类
class CInfoBlock
{
public:
   // IP address,可用作getKey的key值
   uint32_t m_piIP[4];		// IP address, machine read only, not human readable format
   // IPv4 or IPv6
   int m_iIPversion;		// IP version
   // timestamp
   uint64_t m_ullTimeStamp;	// last update time
   // rtt
   int m_iRTT;			// RTT
   // 估算的带宽
   int m_iBandwidth;		// estimated bandwidth
   // 平均丢包率
   int m_iLossRate;		// average loss rate
   // 重排序距离，数据包到达顺序与发送顺序的差异，可以用来识别网络中的重排序现象
   int m_iReorderDistance;	// packet reordering distance
   // 包间时间，数据包之间的时间间隔，用于拥塞控制
   double m_dInterval;		// inter-packet time, congestion control
   // 拥塞控制窗口大小
   double m_dCWnd;		// congestion window size, congestion control

public:
   virtual ~CInfoBlock() {}
   // 重载赋值运算符
   virtual CInfoBlock& operator=(const CInfoBlock& obj);
   // 重载==运算符
   virtual bool operator==(const CInfoBlock& obj);
   // 深拷贝克隆
   virtual CInfoBlock* clone();
   // 获取key值，以IP地址作为key值
   virtual int getKey();
   // 释放资源，未实现
   virtual void release() {}

public:

      // Functionality:
      //    convert sockaddr structure to an integer array
      // Parameters:
      //    0) [in] addr: network address
      //    1) [in] ver: IP version
      //    2) [out] ip: the result machine readable IP address in integer array
      // Returned value:
      //    None.

   // 将sockaddr类型转换为机器可读的IP地址格式
   static void convert(const sockaddr* addr, int ver, uint32_t ip[]);
};


#endif
