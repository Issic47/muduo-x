// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen

#include <muduo/base/Condition.h>

// returns true if time out, false otherwise.
bool muduo::Condition::waitForSeconds(int seconds)
{
  return waitForMilliSeconds(seconds * 1000);
}

bool muduo::Condition::waitForMilliSeconds( int milliseconds )
{
  MutexLock::UnassignGuard ug(mutex_);
  return UV_ETIMEDOUT == uv_cond_timedwait(&pcond_, mutex_.getPthreadMutex(), milliseconds);
}

