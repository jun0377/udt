#ifndef _UDT_TEST_UTIL_H_
#define _UDT_TEST_UTIL_H_


// 妙啊! 使用这种方式，实现自动初始化，自动释放！
struct UDTUpDown{
   UDTUpDown()
   {
      // use this function to initialize the UDT library
      // 初始化UDT
      UDT::startup();
   }
   ~UDTUpDown()
   {
      // use this function to release the UDT library
      UDT::cleanup();
   }
};

#endif
