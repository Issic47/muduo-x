// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMER_H
#define MUDUO_NET_TIMER_H

#include <boost/noncopyable.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <muduo/base/Atomic.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>

namespace muduo
{
namespace net
{
///
/// Internal class for timer event.
///
class Timer : public boost::enable_shared_from_this<Timer>, boost::noncopyable
{
 public:
  Timer(const TimerCallback& cb, 
        Timestamp when, 
        double interval,
        const AfterTimeoutCallback& afterTimeoutCallback)
    : callback_(cb),
      expiration_(when),
      interval_(interval),
      repeat_(interval > 0.0),
      sequence_(s_numCreated_.incrementAndGet()),
      afterTimeoutCallback_(afterTimeoutCallback),
      init_(false)
  { }

  Timer(TimerCallback&& cb, Timestamp when, double interval,
        AfterTimeoutCallback &&afterTimeoutCallback)
    : callback_(std::move(cb)),
      expiration_(when),
      interval_(interval),
      repeat_(interval > 0.0),
      sequence_(s_numCreated_.incrementAndGet()),
      afterTimeoutCallback_(std::move(afterTimeoutCallback)),
      init_(false)
  { }

  ~Timer() 
  {
    if (init_) 
    {
      // FIXME(cbj): need to stop the timer first?
      uv_timer_stop(&timer_);
      uv_close(reinterpret_cast<uv_handle_t*>(&timer_), NULL);
      init_ = false;
    }
  }

  int init(uv_loop_t *loop)
  {
    int err = 0;
    if (!init_) {
      err = uv_timer_init(loop, &timer_);
      // Not need to unref the timer
      //uv_unref(reinterpret_cast<uv_handle_t*>(timer->getUVTimer()));
      init_ = err == 0;
      timer_.data = this;
    }
    return err;
  }

  int start();

  int stop() { assert(init_); return uv_timer_stop(&timer_); }

  Timestamp expiration() const  { return expiration_; }
  bool repeat() const { return repeat_; }
  int64_t sequence() const { return sequence_; }

  void restart(Timestamp now);

  static int64_t numCreated() { return s_numCreated_.get(); }

 private:
  static void uvTimeoutCallback(uv_timer_t *handle) 
  {
    assert(handle->data);
    Timer *timer = static_cast<Timer*>(handle->data);
    timer->callback_();
    timer->afterTimeoutCallback_(timer->shared_from_this());
  }

 private:
  const TimerCallback callback_;
  const AfterTimeoutCallback afterTimeoutCallback_;
  uv_timer_t timer_;
  Timestamp expiration_;
  bool init_;
  const double interval_;
  const bool repeat_;
  const int64_t sequence_;

  static AtomicInt64 s_numCreated_;
};
}
}
#endif  // MUDUO_NET_TIMER_H
