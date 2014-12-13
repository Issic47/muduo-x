// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/Acceptor.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

using namespace muduo;
using namespace muduo::net;

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
  : loop_(loop),
    acceptSocket_(CHECK_NOTNULL(loop_->getFreeSocket())),
    listenning_(false)
{ 
  acceptSocket_.setData(this);

#ifndef NATIVE_WIN32
  acceptSocket_.setReuseAddr(true);
#endif
  acceptSocket_.setReusePort(reuseport);
  acceptSocket_.bindAddress(listenAddr);
}

Acceptor::~Acceptor()
{
  loop_->closeSocketInLoop(acceptSocket_.socket());
}

void Acceptor::listen()
{
  loop_->assertInLoopThread();
  listenning_ = true;
  acceptSocket_.listen(&Acceptor::onNewConnectionCallback);
}

void Acceptor::onNewConnectionCallback( uv_stream_t *server, int status )
{
  if (status) 
  {
    LOG_SYSERR << uv_strerror(status) << " in Acceptor::onNewConnectionCallback";
    return;
  }

  assert(server->data);
  Acceptor *acceptor = static_cast<Acceptor*>(server->data);

  acceptor->loop_->assertInLoopThread();

  EventLoop *nextEventLoop = acceptor->loop_;
  if (acceptor->nextEventLoopCallback_)
  {
    nextEventLoop = acceptor->nextEventLoopCallback_();
    assert(nextEventLoop);
  }

  uv_tcp_t *client = nextEventLoop->getFreeSocket();
  if (nullptr == client) 
  {
    LOG_WARN << "Cannot get free socket from next event loop in Acceptor::onNewConnectionCallback";
    nextEventLoop = acceptor->loop_;
    client = nextEventLoop->getFreeSocket();
    assert(client);
  }

  InetAddress peerAddr;
  int err = acceptor->acceptSocket_.accept(client, &peerAddr);
  if (err)
  {
    LOG_SYSERR << uv_strerror(err) << " in Acceptor::onNewConnectionCallback";
    nextEventLoop->closeSocketInLoop(client);
  } 
  else 
  {
    if (acceptor->newConnectionCallback_)
    {
      acceptor->newConnectionCallback_(client, peerAddr);
    }
    else
    {
      nextEventLoop->closeSocketInLoop(client);
    }
  }
}


