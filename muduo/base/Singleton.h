// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen

#ifndef MUDUO_BASE_SINGLETON_H
#define MUDUO_BASE_SINGLETON_H

#include <boost/noncopyable.hpp>
#include <uv.h>
#include <stdlib.h> // atexit
#include <assert.h>

namespace muduo
{

namespace detail
{
// This doesn't detect inherited member functions!
// http://stackoverflow.com/questions/1966362/sfinae-to-check-for-inherited-member-functions
template<typename T>
struct has_no_destroy
{
  template<typename U> static char Test(decltype(&U::no_destroy));
  template<typename U> static int Test(...);
  static const bool Has = sizeof(Test<T>(nullptr)) == sizeof(char);
};

}

template<typename T>
class Singleton : boost::noncopyable
{
 public:
  static T& instance()
  {
    uv_once(&ponce_, &Singleton::init);
    return *value_;
  }

 private:
  Singleton();
  ~Singleton();

  static void init()
  {
    value_ = new T();
    if (!detail::has_no_destroy<T>::Has)
    {
      int err = ::atexit(destroy);
      (void)(err);
    }
  }

  static void destroy()
  {
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;
    assert(value_ != NULL);
    delete value_;
  }

 private:
  static uv_once_t ponce_;
  static T* value_;
};

template<typename T>
uv_once_t Singleton<T>::ponce_ = UV_ONCE_INIT;

template<typename T>
T* Singleton<T>::value_ = NULL;

}
#endif

