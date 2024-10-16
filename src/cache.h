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

class CCacheItem
{
public:
   virtual ~CCacheItem() {}

public:
   virtual CCacheItem& operator=(const CCacheItem&) = 0;

   // The "==" operator SHOULD only compare key values.
   virtual bool operator==(const CCacheItem&) = 0;

      // Functionality:
      //    get a deep copy clone of the current item
      // Parameters:
      //    None.
      // Returned value:
      //    Pointer to the new item, or NULL if failed.

   virtual CCacheItem* clone() = 0;

      // Functionality:
      //    get a random key value between 0 and MAX_INT to be used for the hash in cache
      // Parameters:
      //    None.
      // Returned value:
      //    A random hash key.

   virtual int getKey() = 0;

   // If there is any shared resources between the cache item and its clone,
   // the shared resource should be released by this function.
   virtual void release() {}
};

template<typename T> class CCache
{
public:
   CCache(int size = 1024):
   m_iMaxSize(size),
   m_iHashSize(size * 3),
   m_iCurrSize(0)
   {
      m_vHashPtr.resize(m_iHashSize);
      // 初始化锁
      CGuard::createMutex(m_Lock);
   }

   ~CCache()
   {
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
      // 类似C++11中的lock_guard
      CGuard cacheguard(m_Lock);

      // 获取key值
      int key = data->getKey();
      if (key < 0)
         return -1;
      if (key >= m_iMaxSize)
         key %= m_iHashSize;

      // 找到匹配的list,list中存储的是迭代器
      const ItemPtrList& item_list = m_vHashPtr[key];
      for (typename ItemPtrList::const_iterator i = item_list.begin(); i != item_list.end(); ++ i)
      {
         if (*data == ***i)
         {
            // copy the cached info
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
      // 类似C++11中的lock_guard
      CGuard cacheguard(m_Lock);

      // 获取key值
      int key = data->getKey();
      if (key < 0)
         return -1;
      if (key >= m_iMaxSize)
         key %= m_iHashSize;

      T* curr = NULL;

      // 找到匹配的list,list中存储的是迭代器
      ItemPtrList& item_list = m_vHashPtr[key];
      for (typename ItemPtrList::iterator i = item_list.begin(); i != item_list.end(); ++ i)
      {
         // 如果key对应的数据已存在，则删除之，并将新数据插入到最前面
         if (*data == ***i)
         {
            // update the existing entry with the new value
            ***i = *data;
            curr = **i;

            // remove the current entry
            m_StorageList.erase(*i);
            item_list.erase(i);

            // re-insert to the front
            m_StorageList.push_front(curr);
            item_list.push_front(m_StorageList.begin());

            return 0;
         }
      }

      // create new entry and insert to front
      curr = data->clone();
      m_StorageList.push_front(curr);
      item_list.push_front(m_StorageList.begin());

      ++ m_iCurrSize;
      // 缓冲容量超出限制，则删除旧的数据
      if (m_iCurrSize >= m_iMaxSize)
      {
         // Cache overflow, remove oldest entry.
         T* last_data = m_StorageList.back();
         int last_key = last_data->getKey() % m_iHashSize;

         item_list = m_vHashPtr[last_key];
         for (typename ItemPtrList::iterator i = item_list.begin(); i != item_list.end(); ++ i)
         {
            if (*last_data == ***i)
            {
               item_list.erase(i);
               break;
            }
         }

         last_data->release();
         delete last_data;
         m_StorageList.pop_back();
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

   // 指定缓存大小（即，最大条目数）
   void setSizeLimit(int size)
   {
      m_iMaxSize = size;
      m_iHashSize = size * 3;
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
      for (typename std::list<T*>::iterator i = m_StorageList.begin(); i != m_StorageList.end(); ++ i)
      {
         (*i)->release();
         delete *i;
      }
      m_StorageList.clear();
      for (typename std::vector<ItemPtrList>::iterator i = m_vHashPtr.begin(); i != m_vHashPtr.end(); ++ i)
         i->clear();
      m_iCurrSize = 0;
   }

private:
   // 存储指针的链表
   std::list<T*> m_StorageList;
   // 声明一个新的类型ItemPtr，它是一个指向m_StorageList中的某一个节点的迭代器
   typedef typename std::list<T*>::iterator ItemPtr;
   // 生命一个新的类型ItemPtrList，它是一个链表，存储着链表迭代器
   typedef std::list<ItemPtr> ItemPtrList;
   // 存储着m_StorageList中每个节点的入口entry
   std::vector<ItemPtrList> m_vHashPtr;

   // 缓存最大大小
   int m_iMaxSize;
   // 哈希表，为什么要是m_iMaxSize的三倍？
   int m_iHashSize;
   int m_iCurrSize;

   pthread_mutex_t m_Lock;

private:
   // 重载拷贝构造函数
   CCache(const CCache&);
   // 重载赋值运算符 
   CCache& operator=(const CCache&);
};


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
   // 重排序距离？什么意思？
   int m_iReorderDistance;	// packet reordering distance
   // 包间时间，拥塞控制
   double m_dInterval;		// inter-packet time, congestion control
   // 拥塞控制窗口大小
   double m_dCWnd;		// congestion window size, congestion control

public:
   virtual ~CInfoBlock() {}
   virtual CInfoBlock& operator=(const CInfoBlock& obj);
   virtual bool operator==(const CInfoBlock& obj);
   virtual CInfoBlock* clone();
   virtual int getKey();
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

   // 将sockaddr类型转换为整数数组
   static void convert(const sockaddr* addr, int ver, uint32_t ip[]);
};


#endif
