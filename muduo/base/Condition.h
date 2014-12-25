// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen

#ifndef MUDUO_BASE_CONDITION_H
#define MUDUO_BASE_CONDITION_H

#include <muduo/base/Mutex.h>

#include <boost/noncopyable.hpp>
#include <uv.h>

namespace muduo
{

class Condition : boost::noncopyable
{
 public:
  explicit Condition(MutexLock& mutex)
    : mutex_(mutex)
  {
    MCHECK(uv_cond_init(&pcond_));
  }

  ~Condition()
  {
    uv_cond_destroy(&pcond_);
  }

  void wait()
  {
    MutexLock::UnassignGuard ug(mutex_);
    uv_cond_wait(&pcond_, mutex_.getPthreadMutex());
  }

  // returns true if time out, false otherwise.
  bool waitForSeconds(int seconds);

  // returns true if time out, false otherwise.
  bool waitForMilliSeconds(int milliseconds);

  void notify()
  {
    uv_cond_signal(&pcond_);
  }

  void notifyAll()
  {
    uv_cond_broadcast(&pcond_);
  }

 private:
  MutexLock& mutex_;
  uv_cond_t pcond_;
};

}
#endif  // MUDUO_BASE_CONDITION_H
