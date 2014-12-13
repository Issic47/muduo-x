// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen

#include <muduo/net/Timer.h>
#include <muduo/base/Logging.h>

namespace muduo
{
namespace net
{
namespace detail
{

inline uint64_t convertToMillisecond(double seconds)
{
  return static_cast<uint64_t>(seconds * 1000);
}

} // !namespace detail
} // !namespace net
} // !namespace muduo

using namespace muduo;
using namespace muduo::net;
using namespace muduo::net::detail;

AtomicInt64 Timer::s_numCreated_;

int Timer::start()
{
  assert(init_);
  double delay = timeDifference(expiration_, Timestamp::now());
  if (delay < 0) 
  {
    LOG_WARN << "Timer's expiration is "  << -delay << "s earlier than now";
    delay = 0.0;  // need to set it to 0.0
  }
  int err = uv_timer_start(
    timer_, &Timer::uvTimeoutCallback, convertToMillisecond(delay), 
    repeat_ ? convertToMillisecond(interval_) : 0);
  return err;
}
