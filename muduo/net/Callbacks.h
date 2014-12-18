// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_CALLBACKS_H
#define MUDUO_NET_CALLBACKS_H

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include <muduo/base/Timestamp.h>

namespace muduo
{

// Adapted from google-protobuf stubs/common.h
// see License in muduo/base/Types.h
template<typename To, typename From>
inline ::boost::shared_ptr<To> down_pointer_cast(const ::boost::shared_ptr<From>& f)
{
  if (false)
  {
    implicit_cast<From*, To*>(0);
  }

#ifndef NDEBUG
  assert(f == NULL || dynamic_cast<To*>(get_pointer(f)) != NULL);
#endif
  return ::boost::static_pointer_cast<To>(f);
}

namespace net
{

// All client visible callbacks go here.

class Buffer;
class TcpConnection;
class Timer;
class UdpSocket;
class InetAddress;
typedef boost::shared_ptr<TcpConnection> TcpConnectionPtr;
typedef boost::shared_ptr<Timer> TimerPtr;
typedef boost::shared_ptr<UdpSocket> UdpSocketPtr;

typedef boost::function<void()> TimerCallback;
typedef boost::function<void (const TcpConnectionPtr&)> ConnectionCallback;
typedef boost::function<void (const TcpConnectionPtr&)> CloseCallback;
typedef boost::function<void (const TcpConnectionPtr&)> WriteCompleteCallback;
typedef boost::function<void (const TcpConnectionPtr&, size_t)> HighWaterMarkCallback;
// the data has been read to (buf, len)
typedef boost::function<void (const TcpConnectionPtr&,
                              Buffer*,
                              Timestamp)> MessageCallback;

void defaultConnectionCallback(const TcpConnectionPtr& conn);
void defaultMessageCallback(const TcpConnectionPtr& conn,
                            Buffer* buffer,
                            Timestamp receiveTime);

typedef boost::function<void (const UdpSocketPtr&)> UdpStartedRecvCallback;
typedef boost::function<void (const UdpSocketPtr&, int)> UdpWriteCompleteCallback;
typedef boost::function<void (const UdpSocketPtr&, size_t)> UdpHighWaterMarkCallback;

typedef boost::function<void (const UdpSocketPtr&, 
                              Buffer*,
                              const InetAddress&, 
                              Timestamp)> UdpMessageCallback;

void defaultUdpMessageCallback(const UdpSocketPtr& socket,
                               Buffer* buffer,
                               const InetAddress& srcAddr,
                               Timestamp receiveTimer);



}
}

#endif  // MUDUO_NET_CALLBACKS_H
