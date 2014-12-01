// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CURRENTTHREAD_H
#define MUDUO_BASE_CURRENTTHREAD_H

#include <stdint.h>
#include <muduo/base/Types.h>
#include <thread>

namespace muduo
{
namespace CurrentThread
{
  // internal
  extern thread_local pid_t t_cachedTid;
  extern thread_local char t_tidString[32];
  extern thread_local int t_tidStringLength;
  extern thread_local const char* t_threadName;
  void cacheTid();

  inline pid_t tid()
  {
    if (t_cachedTid == 0)
    {
      cacheTid();
    }
    return t_cachedTid;
  }

  inline const char* tidString() // for logging
  {
    return t_tidString;
  }

  inline int tidStringLength() // for logging
  {
    return t_tidStringLength;
  }

  inline const char* name()
  {
    return t_threadName;
  }

  bool isMainThread();

  void sleepUsec(int64_t usec);
}
}

#endif
