// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMERQUEUE_H
#define MUDUO_NET_TIMERQUEUE_H

#include <list>
#include <set>

#include <boost/noncopyable.hpp>

#include <muduo/base/Mutex.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>

namespace muduo
{
namespace net
{

class EventLoop;
class Timer;
class TimerId;

///
/// A best efforts timer queue.
/// No guarantee that the callback will be on time.
///
class TimerQueue : boost::noncopyable
{
 public:
  TimerQueue(EventLoop* loop);
  ~TimerQueue();

  ///
  /// Schedules the callback to be run at given time,
  /// repeats if @c interval > 0.0.
  ///
  /// Must be thread safe. Usually be called from other threads.
  TimerId addTimer(const TimerCallback& cb,
                   Timestamp when,
                   double interval);

  TimerId addTimer(TimerCallback&& cb,
                   Timestamp when,
                   double interval);

  void cancel(TimerId timerId);

 private:
  typedef std::set<TimerPtr> TimerList;

  void afterTimeoutCallback(TimerPtr timer);

  void addTimerInLoop(TimerPtr timer);
  void cancelInLoop(TimerId timerId);
  void removeTimer(TimerPtr timer);

 private:
  EventLoop* loop_;
  TimerList allocTimers_;
};

}
}
#endif  // MUDUO_NET_TIMERQUEUE_H
