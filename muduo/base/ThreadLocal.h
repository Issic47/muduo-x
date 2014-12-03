// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADLOCAL_H
#define MUDUO_BASE_THREADLOCAL_H

#include <muduo/base/Mutex.h>  // MCHECK

#include <boost/noncopyable.hpp>

namespace muduo
{

template<typename T>
class ThreadLocal : boost::noncopyable
{
 public:
  ThreadLocal()
  {
    // FIXME(cbj): call destructor when thread exit
    MCHECK(uv_key_create(&pkey_));
  }

  ~ThreadLocal()
  {
    destructor(uv_key_get(&pkey_));
    uv_key_delete(&pkey_);
  }

  T& value()
  {
    T* perThreadValue = static_cast<T*>(uv_key_get(&pkey_));
    if (!perThreadValue)
    {
      T* newObj = new T();
      uv_key_set(&pkey_, newObj);
      perThreadValue = newObj;
    }
    return *perThreadValue;
  }

 private:

  static void destructor(void *x)
  {
    if (x == NULL) return;
    T* obj = static_cast<T*>(x);
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;
    delete obj;
  }

 private:
  uv_key_t pkey_;
};

}
#endif
