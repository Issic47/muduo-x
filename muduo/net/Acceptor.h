// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_ACCEPTOR_H
#define MUDUO_NET_ACCEPTOR_H

#include <boost/function.hpp>
#include <boost/noncopyable.hpp>

#include <muduo/net/TcpSocket.h>

namespace muduo
{
namespace net
{

class EventLoop;
class InetAddress;

///
/// Acceptor of incoming TCP connections.
///
class Acceptor : boost::noncopyable
{
 public:
  typedef boost::function<void (uv_tcp_t*,
                                const InetAddress&)> NewConnectionCallback;

  typedef boost::function<EventLoop*()> NextEventLoopCallback;

  Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);
  ~Acceptor();

  void setNewConnectionCallback(const NewConnectionCallback& cb)
  { newConnectionCallback_ = cb; }

  void setNextEventLoopCallback(const NextEventLoopCallback &cb)
  { nextEventLoopCallback_ = cb; }

  bool listenning() const { return listenning_; }
  void listen();

 private:
  static void onNewConnectionCallback(uv_stream_t *server, int status);

  EventLoop* loop_;
  TcpSocket acceptSocket_;
  NewConnectionCallback newConnectionCallback_;
  NextEventLoopCallback nextEventLoopCallback_;
  bool listenning_;
};

}
}

#endif  // MUDUO_NET_ACCEPTOR_H
