// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/TcpServer.h>

#include <muduo/base/Logging.h>
#include <muduo/net/Acceptor.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThreadPool.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

TcpServer::TcpServer(EventLoop* loop,
                     const InetAddress& listenAddr,
                     const string& nameArg,
                     Option option)
  : loop_(CHECK_NOTNULL(loop)),
    hostport_(listenAddr.toIpPort()),
    name_(nameArg),
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
    threadPool_(new EventLoopThreadPool(loop)),
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
    nextConnId_(1)
{
  acceptor_->setNewConnectionCallback(
      boost::bind(&TcpServer::newConnection, this, _1, _2));
  acceptor_->setNextEventLoopCallback(
      boost::bind(&TcpServer::nextEventLoop, this));
}

TcpServer::~TcpServer()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

  for (ConnectionMap::iterator it(connections_.begin());
      it != connections_.end(); ++it)
  {
    TcpConnectionPtr conn = it->second;
    it->second.reset();
    conn->getLoop()->runInLoop(
      boost::bind(&TcpConnection::connectDestroyed, conn));
    conn.reset();
  }
}

void TcpServer::setThreadNum(int numThreads)
{
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}

void TcpServer::start()
{
  if (started_.getAndSet(1) == 0)
  {
    threadPool_->start(threadInitCallback_);

    assert(!acceptor_->listenning());
    loop_->runInLoop(
        boost::bind(&Acceptor::listen, get_pointer(acceptor_)));
  }
}

void TcpServer::newConnection(uv_tcp_t *socket, const InetAddress& peerAddr)
{
  loop_->assertInLoopThread();
  assert(socket->loop);
  assert(socket->loop->data);
  EventLoop* ioLoop = static_cast<EventLoop*>(socket->loop->data);
  char buf[32];
  snprintf(buf, sizeof buf, ":%s#%d", hostport_.c_str(), nextConnId_);
  ++nextConnId_;
  string connName = name_ + buf;

  LOG_INFO << "TcpServer::newConnection [" << name_
           << "] - new connection [" << connName
           << "] from " << peerAddr.toIpPort();

  
  sa addr;
  int len = sizeof(addr);
  int err = uv_tcp_getsockname(socket, &addr.u.sa, &len);
  if (err) 
  {
    LOG_SYSERR << uv_strerror(err) << " in TcpServer::newConnection";
  }

  InetAddress localAddr(addr);
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  TcpConnectionPtr conn(new TcpConnection(connName,
                                          socket,
                                          localAddr,
                                          peerAddr));
  connections_[connName] = conn;
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(
      boost::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe
  ioLoop->runInLoop(boost::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
  // FIXME: unsafe
  loop_->runInLoop(boost::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);
  EventLoop* ioLoop = conn->getLoop();
  ioLoop->queueInLoop(
      boost::bind(&TcpConnection::connectDestroyed, conn));
}

EventLoop* muduo::net::TcpServer::nextEventLoop()
{
  return threadPool_->getNextLoop();
}
