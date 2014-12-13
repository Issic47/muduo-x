// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_CONNECTOR_H
#define MUDUO_NET_CONNECTOR_H

#include <muduo/net/InetAddress.h>

#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

namespace muduo
{
namespace net
{

class EventLoop;

class Connector : boost::noncopyable,
                  public boost::enable_shared_from_this<Connector>
{
 public:
  typedef boost::function<void (uv_tcp_t*)> NewConnectionCallback;

  Connector(EventLoop* loop, const InetAddress& serverAddr);
  ~Connector();

  void setNewConnectionCallback(const NewConnectionCallback& cb)
  { newConnectionCallback_ = cb; }
  void setNewConnectionCallback(NewConnectionCallback&& cb)
  { newConnectionCallback_ = std::move(cb); }

  void start();  // can be called in any thread
  void restart();  // must be called in loop thread
  void stop();  // can be called in any thread

  const InetAddress& serverAddress() const { return serverAddr_; }

 private:
  static void onConnectCallback(uv_connect_t *req, int status);

 private:
  typedef struct ConnectRequest
  {
    boost::weak_ptr<Connector> connector;
    uv_connect_t req;
  } ConnectRequest;

  enum States { kDisconnected, kConnecting, kConnected };
  static const int kMaxRetryDelayMs = 30*1000;
  static const int kInitRetryDelayMs = 500;

  void setState(States s) { state_ = s; }
  void startInLoop();
  void stopInLoop();
  void connect();
  void connecting();
  void handleWrite();
  void handleError();
  void retry();
  int removeAndResetChannel();
  void resetChannel();
  void handleConnectError(int err);

  EventLoop* loop_;
  InetAddress serverAddr_;
  bool connect_; // atomic
  States state_;  // FIXME: use atomic variable
  uv_tcp_t *socket_;
  NewConnectionCallback newConnectionCallback_;
  int retryDelayMs_;
};

typedef boost::shared_ptr<Connector> ConnectorPtr;

}
}

#endif  // MUDUO_NET_CONNECTOR_H
