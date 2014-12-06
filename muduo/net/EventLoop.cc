// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/EventLoop.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/Channel.h>
#include <muduo/net/Poller.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TimerQueue.h>

#include <boost/bind.hpp>

#include <signal.h>
#include <sys/eventfd.h>

using namespace muduo;
using namespace muduo::net;

namespace
{
thread_local EventLoop* t_loopInThisThread = 0;

const int kPollTimeMs = 10000;

int createEventfd()
{
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0)
  {
    LOG_SYSERR << "Failed in eventfd";
    abort();
  }
  return evtfd;
}

#pragma GCC diagnostic ignored "-Wold-style-cast"
class IgnoreSigPipe
{
 public:
  IgnoreSigPipe()
  {
    ::signal(SIGPIPE, SIG_IGN);
    // LOG_TRACE << "Ignore SIGPIPE";
  }
};
#pragma GCC diagnostic error "-Wold-style-cast"

IgnoreSigPipe initObj;
}

EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
  return t_loopInThisThread;
}

EventLoop::EventLoop()
  : looping_(false),
    quit_(false),
    eventHandling_(false),
    callingPendingFunctors_(false),
    iteration_(0),
    threadId_(CurrentThread::tid()),
    poller_(Poller::newDefaultPoller(this)),
    timerQueue_(new TimerQueue(this)),
    wakeupFd_(createEventfd()),
    wakeupChannel_(new Channel(this, wakeupFd_)),
    currentActiveChannel_(NULL)
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

  } while (false);
  
  if (err) 
  {
    LOG_FATAL << "Event Loop init failed with error: " << uv_strerror(err) 
              << " in thread " << threadId_;
  }
  

  wakeupChannel_->setReadCallback(
      boost::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd
  wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
  LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
            << " destructs in thread " << CurrentThread::tid();

  uv_walk(&loop_, &EventLoop::closeWalkCallback, NULL);
  uv_run(&loop_, UV_RUN_DEFAULT);

  int err = uv_loop_close(&loop_);
  if (err) 
  {
    LOG_ERROR << "EventLoop " << this << " should be stopped before destruct";
  }

  t_loopInThisThread = NULL;
}

void muduo::net::EventLoop::loopPrepareCallback( uv_prepare_t *handle )
{
  assert(handle->data);
  EventLoop *loop = static_cast<EventLoop*>(handle->data);
  ++loop->iteration_;
}

void muduo::net::EventLoop::loopCheckCallback( uv_check_t *handle )
{
  assert(handle->data);
  EventLoop *loop = static_cast<EventLoop*>(handle->data);
  loop->doPendingFunctors();
}

void muduo::net::EventLoop::loopAsyncCallback( uv_async_t *handle )
{
  assert(handle->data);
  EventLoop *loop = static_cast<EventLoop*>(handle->data);
  LOG_TRACE << "EventLoop " << loop << " is wakeup";
}

void muduo::net::EventLoop::loopTimeoutCallback( uv_timer_t &handle )
{

}

void muduo::net::EventLoop::closeWalkCallback(uv_handle_t *handle, void *arg)
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

#ifdef __GXX_EXPERIMENTAL_CXX0X__
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
#endif // __GXX_EXPERIMENTAL_CXX0X__

void EventLoop::cancel(TimerId timerId)
{
  return timerQueue_->cancel(timerId);
}

void EventLoop::updateChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  if (eventHandling_)
  {
    assert(currentActiveChannel_ == channel ||
        std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
  }
  poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  return poller_->hasChannel(channel);
}

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
    LOG_ERROR << "A error occured when call uv_sync_send in EventLoop::wakeup():" 
              << uv_strerror(err);
  }
}

void EventLoop::printActiveChannels() const
{
  for (ChannelList::const_iterator it = activeChannels_.begin();
      it != activeChannels_.end(); ++it)
  {
    const Channel* ch = *it;
    LOG_TRACE << "{" << ch->reventsToString() << "} ";
  }
}

