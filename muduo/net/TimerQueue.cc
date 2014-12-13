// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include <muduo/net/TimerQueue.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Timer.h>
#include <muduo/net/TimerId.h>

#include <boost/bind.hpp>
#include <boost/make_shared.hpp>

using namespace muduo;
using namespace muduo::net;

TimerQueue::TimerQueue(EventLoop* loop)
  : loop_(loop),
    freeTimers_()
{
}

TimerQueue::~TimerQueue()
{
}

TimerId TimerQueue::addTimer(const TimerCallback& cb,
                             Timestamp when,
                             double interval)
{
  TimerPtr timer = nullptr;
  if (freeTimers_.empty()) 
  {
    timer = boost::make_shared<Timer>(cb, when, interval,
      boost::bind(&TimerQueue::afterTimeoutCallback, this, _1));
    allocTimers_.push_back(timer);
  }
  else
  {
    timer = freeTimers_.back();
    freeTimers_.pop_back();
  }

  loop_->runInLoop(
      boost::bind(&TimerQueue::addTimerInLoop, this, timer));
  return TimerId(timer, timer->sequence());
}

TimerId TimerQueue::addTimer(TimerCallback&& cb,
                             Timestamp when,
                             double interval)
{
  TimerPtr timer = nullptr;
  if (freeTimers_.empty()) 
  {
    timer = boost::make_shared<Timer>(std::move(cb), when, interval, 
      boost::bind(&TimerQueue::afterTimeoutCallback, this, _1));
    allocTimers_.push_back(timer);
  }
  else
  {
    timer = freeTimers_.back();
    freeTimers_.pop_back();
  }
    
  loop_->runInLoop(
      boost::bind(&TimerQueue::addTimerInLoop, this, timer));
  return TimerId(timer, timer->sequence());
}

void TimerQueue::cancel(TimerId timerId)
{
  loop_->runInLoop(
      boost::bind(&TimerQueue::cancelInLoop, this, timerId));
}

void TimerQueue::addTimerInLoop(TimerPtr timer)
{
  loop_->assertInLoopThread();
  
  int err = 0;
  do 
  {
    err = timer->init(loop_->getUVLoop());
    if (err) break;

    err = timer->start();
    if (err) break;

  } while (false);
  
  if (err) 
  {
    LOG_ERROR << uv_strerror(err) << " in TimerQueue::addTimerInLoop";
  }
}

void TimerQueue::afterTimeoutCallback( TimerPtr timer )
{
  assert(timer);
  if (!timer->repeat()) {
    freeTimers_.push_back(timer);
  }
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
  loop_->assertInLoopThread();
  auto timer = timerId.timer_.lock(); 
  if (timer) 
  {
    timer->stop();
    freeTimers_.push_back(timer);
  }
  else
  {
    LOG_WARN << "Timer is destructed before cancel";
  }
}

