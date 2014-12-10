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
    uvSocket_(new uv_tcp_t),
    acceptSocket_(uvSocket_),
    listenning_(false)
{ 
  int err = uv_tcp_init(loop_->getUVLoop(), uvSocket_);
  if (err)
    LOG_SYSFATAL << uv_strerror(err);

#ifndef NATIVE_WIN32
  acceptSocket_.setReuseAddr(true);
#endif
  acceptSocket_.setReusePort(reuseport);
  acceptSocket_.bindAddress(listenAddr);
}

Acceptor::~Acceptor()
{
  uv_close(reinterpret_cast<uv_handle_t*>(uvSocket_), 
    &Acceptor::onHandleCloseCallback);
}

void Acceptor::onHandleCloseCallback( uv_handle_t *handle )
{
  assert(uv_is_closing(handle));
  delete handle;
}

void Acceptor::listen()
{
  loop_->assertInLoopThread();
  listenning_ = true;
  uvSocket_->data = this;
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

  // FIXME(cbj): assign a loop for the client.
  uv_tcp_t *client = new uv_tcp_t;
  int err = uv_tcp_init(acceptor->loop_->getUVLoop(), client);
  if (err)
    LOG_SYSFATAL << uv_strerror(err) << " in Acceptor::onNewConnectionCallback";

  InetAddress peerAddr;
  err = acceptor->acceptSocket_.accept(client, &peerAddr);
  if (err)
  {
    LOG_SYSERR << uv_strerror(err) << " in Acceptor::onNewConnectionCallback";
    delete client; // TODO: use free list
  } 
  else 
  {
    if (acceptor->newConnectionCallback_)
    {
      acceptor->newConnectionCallback_(client, peerAddr);
    }
    else
    {
      uv_close(reinterpret_cast<uv_handle_t*>(client),
        &Acceptor::onHandleCloseCallback);
    }
  }
}


