// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen

#ifndef MUDUO_BASE_THREADLOCALSINGLETON_H
#define MUDUO_BASE_THREADLOCALSINGLETON_H

#include <muduo/base/Types.h>
#include <boost/noncopyable.hpp>
#include <assert.h>

namespace muduo
{

template<typename T>
class ThreadLocalSingleton : boost::noncopyable
{
 public:

  static T& instance()
  {
    if (!t_value_)
    {
      t_value_ = new T();
      deleter_.set(t_value_);
    }
    return *t_value_;
  }

  static T* pointer()
  {
    return t_value_;
  }

  // WARNING: need to call destroy before the thread exit
  static void destroy() 
  {
    destructor(t_value_);
  }

 private:
  ThreadLocalSingleton();
  ~ThreadLocalSingleton();

  static void destructor(void* obj)
  {
    assert(obj == t_value_);
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;
    delete t_value_;
    t_value_ = 0;
  }

  class Deleter
  {
   public:
    Deleter()
    {
      uv_key_create(&pkey_);
    }

    ~Deleter()
    {
      destructor(uv_key_get(&pkey_));
      uv_key_delete(&pkey_);
    }

    void set(T* newObj)
    {
      assert(uv_key_get(&pkey_) == NULL);
      uv_key_set(&pkey_, newObj);
    }

    uv_key_t pkey_;
  };

  static thread_local T* t_value_;
  static Deleter deleter_;
};

template<typename T>
thread_local T* ThreadLocalSingleton<T>::t_value_ = 0;

template<typename T>
typename ThreadLocalSingleton<T>::Deleter ThreadLocalSingleton<T>::deleter_;

}
#endif
