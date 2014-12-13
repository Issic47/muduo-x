// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOP_H
#define MUDUO_NET_EVENTLOOP_H

#include <vector>

#include <boost/any.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

#include <muduo/base/Mutex.h>
#include <muduo/base/CurrentThread.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/TimerId.h>

#include <uv.h>
#include <atomic>

namespace muduo
{
namespace net
{

//class Channel;
class Poller;
class TimerQueue;

///
/// Reactor, at most one per thread.
///
/// This is an interface class, so don't expose too much details.
class EventLoop : boost::noncopyable
{
 public:
  typedef boost::function<void()> Functor;

  EventLoop();
  ~EventLoop();  // force out-line dtor, for scoped_ptr members.

  ///
  /// Loops forever.
  ///
  /// Must be called in the same thread as creation of the object.
  ///
  void loop();

  /// Quits loop.
  ///
  /// This is not 100% thread safe, if you call through a raw pointer,
  /// better to call through shared_ptr<EventLoop> for 100% safety.
  void quit();

  ///
  /// Time when poll returns, usually means data arrival.
  ///
  Timestamp pollReturnTime() const {
    return addTime(initTimeStamp_, uv_now(&loop_) * 1000.0);
  }

  int64_t iteration() const { return iteration_; }

  /// Runs callback immediately in the loop thread.
  /// It wakes up the loop, and run the cb.
  /// If in the same loop thread, cb is run within the function.
  /// Safe to call from other threads.
  void runInLoop(const Functor& cb);
  void runInLoop(Functor&& cb);

  /// Queues callback in the loop thread.
  /// Runs after finish pooling.
  /// Safe to call from other threads.
  void queueInLoop(const Functor& cb);
  void queueInLoop(Functor&& cb);

  // timers

  ///
  /// Runs callback at 'time'.
  /// Safe to call from other threads.
  ///
  TimerId runAt(const Timestamp& time, const TimerCallback& cb);
  ///
  /// Runs callback after @c delay seconds.
  /// Safe to call from other threads.
  ///
  TimerId runAfter(double delay, const TimerCallback& cb);
  ///
  /// Runs callback every @c interval seconds.
  /// Safe to call from other threads.
  ///
  TimerId runEvery(double interval, const TimerCallback& cb);
  ///
  /// Cancels the timer.
  /// Safe to call from other threads.
  ///
  void cancel(TimerId timerId);

  TimerId runAt(const Timestamp& time, TimerCallback&& cb);
  TimerId runAfter(double delay, TimerCallback&& cb);
  TimerId runEvery(double interval, TimerCallback&& cb);

  uv_loop_t *getUVLoop() { return &loop_; }

  // internal usage
  void wakeup();
  //void updateChannel(Channel* channel);
  //void removeChannel(Channel* channel);
  //bool hasChannel(Channel* channel);

  // pid_t threadId() const { return threadId_; }
  void assertInLoopThread()
  {
    if (!isInLoopThread())
    {
      abortNotInLoopThread();
    }
  }
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
  // bool callingPendingFunctors() const { return callingPendingFunctors_; }
  //bool eventHandling() const { return eventHandling_; }

  void setContext(const boost::any& context)
  { context_ = context; }

  const boost::any& getContext() const
  { return context_; }

  boost::any* getMutableContext()
  { return &context_; }

  uv_tcp_t* getFreeSocket();
  void closeSocketInLoop(uv_tcp_t *socket);

  static EventLoop* getEventLoopOfCurrentThread();

 private:
  static void loopPrepareCallback(uv_prepare_t *handle);
  static void loopCheckCallback(uv_check_t *handle);
  static void loopAsyncCallback(uv_async_t *handle);
  static void closeWalkCallback(uv_handle_t *handle, void *arg);
  static void closeCallback(uv_handle_t *handle);

  void abortNotInLoopThread();
  void doPendingFunctors();

  void createFreeSocket();
  void closeSocket(uv_tcp_t *socket);

  //void printActiveChannels() const; // DEBUG

  //typedef std::vector<Channel*> ChannelList;

  bool looping_; /* atomic */
  bool quit_; /* atomic and shared between threads, okay on x86, I guess. */
  //bool eventHandling_; /* atomic */
  bool callingPendingFunctors_; /* atomic */
  uv_loop_t loop_;

  uv_prepare_t prepare_handle_; // use for doing iteration count
  int64_t iteration_;

  uv_check_t check_handle_; // use for doing pending functors
  MutexLock mutex_;
  std::vector<Functor> pendingFunctors_; // @GuardedBy mutex_

  uv_async_t async_handle_; // for wakeup the loop
  
  const pid_t threadId_;

  uint64_t initLoopTime_;
  Timestamp initTimeStamp_;

  //boost::scoped_ptr<Poller> poller_;
  boost::scoped_ptr<TimerQueue> timerQueue_;

  std::atomic<uv_tcp_t*> freeSocket_;

  boost::any context_;

  //// scratch variables
  //ChannelList activeChannels_;
  //Channel* currentActiveChannel_;
};

}
}
#endif  // MUDUO_NET_EVENTLOOP_H
