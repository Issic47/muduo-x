// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen

#include <muduo/net/EventLoop.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
//#include <muduo/net/Channel.h>
//#include <muduo/net/Poller.h>
#include <muduo/net/TimerQueue.h>

#include <boost/bind.hpp>

#include <signal.h>

using namespace muduo;
using namespace muduo::net;

namespace
{
thread_local EventLoop* t_loopInThisThread = 0;

const int kPollTimeMs = 10000;

#ifndef NATIVE_WIN32

#if defined(__GCC__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

class IgnoreSigPipe
{
 public:
  IgnoreSigPipe()
  {
    ::signal(SIGPIPE, SIG_IGN);
    // LOG_TRACE << "Ignore SIGPIPE";
  }
};

#if defined(__GCC__) || defined(__GNUC__)
#pragma GCC diagnostic error "-Wold-style-cast"
#endif

IgnoreSigPipe initObj;

#endif // NATIVE_WIN32
}

EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
  return t_loopInThisThread;
}

EventLoop::EventLoop()
  : looping_(false),
    quit_(false),
    //eventHandling_(false),
    callingPendingFunctors_(false),
    iteration_(0),
    threadId_(CurrentThread::tid()),
    //poller_(Poller::newDefaultPoller(this)),
    initLoopTime_(0),
    timerQueue_(new TimerQueue(this)),
    freeTcpSocket_(new uv_tcp_t),
    freeUdpSocket_(new uv_udp_t)
    //currentActiveChannel_(NULL)
{
  LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
  if (t_loopInThisThread)
  {
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  }
  else
  {
    t_loopInThisThread = this;
  }

  int err = 0;
  do 
  {
    err = uv_loop_init(&loop_);
    loop_.data = this;
    if (err) break;

    // initialize prepare handle
    err = uv_prepare_init(&loop_, &prepare_handle_);
    if (err) break;
    prepare_handle_.data = this;
    err = uv_prepare_start(&prepare_handle_, &EventLoop::loopPrepareCallback);
    if (err) break;

    // initialize check handle
    err = uv_check_init(&loop_, &check_handle_);
    if (err) break;
    check_handle_.data = this;
    err = uv_check_start(&check_handle_, &EventLoop::loopCheckCallback);
    if (err) break;

    // initialize async handle
    err = uv_async_init(&loop_, &async_handle_, &EventLoop::loopAsyncCallback);
    if (err) break;
    async_handle_.data = this;

    err = uv_tcp_init(&loop_, freeTcpSocket_);
    if (err) break;

    err = uv_udp_init(&loop_, freeUdpSocket_);
    if (err) break;

  } while (false);
  
  if (err) 
  {
    LOG_FATAL << "Event Loop init failed with error: " << uv_strerror(err) 
              << " in thread " << threadId_;
  }
}

EventLoop::~EventLoop()
{
  LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
            << " destructs in thread " << CurrentThread::tid();

  uv_tcp_t *tcpSocket = freeTcpSocket_.exchange(nullptr);
  uv_udp_t *udpSocket = freeUdpSocket_.exchange(nullptr);

  uv_walk(&loop_, &EventLoop::closeWalkCallback, NULL);
  uv_run(&loop_, UV_RUN_DEFAULT);

  if (tcpSocket)
  {
    delete tcpSocket; // release socket
  }
  if (udpSocket)
  {
    delete udpSocket;
  }

  int err = uv_loop_close(&loop_);
  if (err) 
  {
    LOG_ERROR << "EventLoop " << this << " should be stopped before destruct";
  }

  t_loopInThisThread = NULL;
}

void EventLoop::loopPrepareCallback( uv_prepare_t *handle )
{
  assert(handle->data);
  EventLoop *loop = static_cast<EventLoop*>(handle->data);
  ++loop->iteration_;
  loop->doPendingFunctors();
}

void EventLoop::loopCheckCallback( uv_check_t *handle )
{
  assert(handle->data);
  EventLoop *loop = static_cast<EventLoop*>(handle->data);
  loop->doPendingFunctors();
}

void EventLoop::loopAsyncCallback( uv_async_t *handle )
{
  assert(handle->data);
  EventLoop *loop = static_cast<EventLoop*>(handle->data);
  LOG_TRACE << "EventLoop " << loop << " is wakeup";
}

void EventLoop::closeWalkCallback(uv_handle_t *handle, void *arg)
{
  if (!uv_is_closing(handle))
    uv_close(handle, NULL);
}

void EventLoop::doPendingFunctors()
{
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;

  {
    MutexLockGuard lock(mutex_);
    functors.swap(pendingFunctors_);
  }

  for (size_t i = 0; i < functors.size(); ++i)
  {
    functors[i]();
  }
  callingPendingFunctors_ = false;
}

void EventLoop::loop()
{
  assert(!looping_);
  assertInLoopThread();
  looping_ = true;
  quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
  LOG_TRACE << "EventLoop " << this << " start looping";

  uv_update_time(&loop_);
  initTimeStamp_ = Timestamp::now();
  initLoopTime_ = uv_now(&loop_);

  uv_run(&loop_, UV_RUN_DEFAULT);

  //while (!quit_)
  //{
  //  activeChannels_.clear();
  //  pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
  //  ++iteration_;
  //  if (Logger::logLevel() <= Logger::TRACE)
  //  {
  //    printActiveChannels();
  //  }
  //  // TODO sort channel by priority
  //  eventHandling_ = true;
  //  for (ChannelList::iterator it = activeChannels_.begin();
  //      it != activeChannels_.end(); ++it)
  //  {
  //    currentActiveChannel_ = *it;
  //    currentActiveChannel_->handleEvent(pollReturnTime_);
  //  }
  //  currentActiveChannel_ = NULL;
  //  eventHandling_ = false;
  //  doPendingFunctors();
  //}

  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}

void EventLoop::quit()
{
  quit_ = true;
  // There is a chance that loop() just executes while(!quit_) and exists,
  // then EventLoop destructs, then we are accessing an invalid object.
  // Can be fixed using mutex_ in both places.
  uv_stop(&loop_);
  if (!isInLoopThread())
  {
    wakeup();
  }
}

void EventLoop::runInLoop(const Functor& cb)
{
  if (isInLoopThread())
  {
    cb();
  }
  else
  {
    queueInLoop(cb);
  }
}

void EventLoop::queueInLoop(const Functor& cb)
{
  {
  MutexLockGuard lock(mutex_);
  pendingFunctors_.push_back(cb);
  }

  if (!isInLoopThread() || callingPendingFunctors_)
  {
    wakeup();
  }
}

TimerId EventLoop::runAt(const Timestamp& time, const TimerCallback& cb)
{
  return timerQueue_->addTimer(cb, time, 0.0);
}

TimerId EventLoop::runAfter(double delay, const TimerCallback& cb)
{
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, cb);
}

TimerId EventLoop::runEvery(double interval, const TimerCallback& cb)
{
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(cb, time, interval);
}

// FIXME: remove duplication
void EventLoop::runInLoop(Functor&& cb)
{
  if (isInLoopThread())
  {
    cb();
  }
  else
  {
    queueInLoop(std::move(cb));
  }
}

void EventLoop::queueInLoop(Functor&& cb)
{
  {
  MutexLockGuard lock(mutex_);
  pendingFunctors_.push_back(std::move(cb));  // emplace_back
  }

  if (!isInLoopThread() || callingPendingFunctors_)
  {
    wakeup();
  }
}

TimerId EventLoop::runAt(const Timestamp& time, TimerCallback&& cb)
{
  return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

TimerId EventLoop::runAfter(double delay, TimerCallback&& cb)
{
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(double interval, TimerCallback&& cb)
{
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(std::move(cb), time, interval);
}

void EventLoop::cancel(TimerId timerId)
{
  return timerQueue_->cancel(timerId);
}

//void EventLoop::updateChannel(Channel* channel)
//{
//  assert(channel->ownerLoop() == this);
//  assertInLoopThread();
//  poller_->updateChannel(channel);
//}
//
//void EventLoop::removeChannel(Channel* channel)
//{
//  assert(channel->ownerLoop() == this);
//  assertInLoopThread();
//  if (eventHandling_)
//  {
//    assert(currentActiveChannel_ == channel ||
//        std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
//  }
//  poller_->removeChannel(channel);
//}
//
//bool EventLoop::hasChannel(Channel* channel)
//{
//  assert(channel->ownerLoop() == this);
//  assertInLoopThread();
//  return poller_->hasChannel(channel);
//}

void EventLoop::abortNotInLoopThread()
{
  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " <<  CurrentThread::tid();
}

void EventLoop::wakeup()
{
  int err = uv_async_send(&async_handle_);
  if (err) 
  {
    LOG_ERROR << uv_strerror(err) << " in EventLoop::wakeup()";
  }
}

//void EventLoop::printActiveChannels() const
//{
//  for (ChannelList::const_iterator it = activeChannels_.begin();
//      it != activeChannels_.end(); ++it)
//  {
//    const Channel* ch = *it;
//    LOG_TRACE << "{" << ch->reventsToString() << "} ";
//  }
//}

uv_tcp_t* EventLoop::getFreeTcpSocket()
{
  // FIXME(cbj): currently only one free socket available.
  uv_tcp_t *tmp = freeTcpSocket_.exchange(nullptr);
  runInLoop(boost::bind(&EventLoop::createFreeTcpSocket, this));
  return tmp;
}

void EventLoop::createFreeTcpSocket()
{
  assertInLoopThread();

  if (!freeTcpSocket_.load())
  {
    uv_tcp_t *newSocket = new uv_tcp_t;
    int err = uv_tcp_init(&loop_, newSocket);
    if (err)
    {
      LOG_SYSERR << uv_strerror(err) << " in EventLoop::createFreeSocket";
      return;
    }
    freeTcpSocket_.exchange(newSocket);
  }
}

void EventLoop::closeSocketInLoop( uv_tcp_t *socket )
{
  runInLoop(boost::bind(&EventLoop::closeTcpSocket, this, socket));
}

void EventLoop::closeTcpSocket(uv_tcp_t *socket)
{
  assertInLoopThread();
  assert(socket);
  uv_close(reinterpret_cast<uv_handle_t*>(socket), 
           &EventLoop::closeCallback);
}

void EventLoop::closeCallback( uv_handle_t *handle )
{
  assert(uv_is_closing(handle));
  delete handle;
}

uv_udp_t* EventLoop::getFreeUdpSocket()
{
  // FIXME(cbj): currently only one free socket available.
  uv_udp_t *tmp = freeUdpSocket_.exchange(nullptr);
  runInLoop(boost::bind(&EventLoop::createFreeUdpSocket, this));
  return tmp;
}

void EventLoop::createFreeUdpSocket()
{
  assertInLoopThread();

  if (!freeUdpSocket_.load())
  {
    uv_udp_t *newSocket = new uv_udp_t;
    int err = uv_udp_init(&loop_, newSocket);
    if (err)
    {
      LOG_SYSERR << uv_strerror(err) << " in EventLoop::createFreeSocket";
      return;
    }
    freeUdpSocket_.exchange(newSocket);
  }
}

void EventLoop::closeSocketInLoop( uv_udp_t *socket )
{
  assertInLoopThread();
  assert(socket);
  uv_close(reinterpret_cast<uv_handle_t*>(socket), 
    &EventLoop::closeCallback);
}

void EventLoop::closeUdpSocket( uv_udp_t *socket )
{
  assertInLoopThread();
  assert(socket);
  uv_close(reinterpret_cast<uv_handle_t*>(socket), 
    &EventLoop::closeCallback);
}


