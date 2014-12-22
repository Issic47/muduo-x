// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen
//

#include <muduo/net/Connector.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpSocket.h>
//#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <uv.h>

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

Connector::Connector(EventLoop* loop, const InetAddress& serverAddr)
  : loop_(loop),
    serverAddr_(serverAddr),
    connect_(false),
    state_(kDisconnected),
    retryDelayMs_(kInitRetryDelayMs),
    socket_(nullptr)
{
  LOG_DEBUG << "ctor[" << this << "]";
}

Connector::~Connector()
{
  LOG_DEBUG << "dtor[" << this << "]";
  assert(state_ == kDisconnected);
}

void Connector::start()
{
  connect_ = true;
  loop_->runInLoop(boost::bind(&Connector::startInLoop, this)); // FIXME: unsafe
}

void Connector::startInLoop()
{
  loop_->assertInLoopThread();
  assert(state_ == kDisconnected);
  if (connect_)
  {
    connect();
  }
  else
  {
    LOG_DEBUG << "do not connect";
  }
}

void Connector::stop()
{
  connect_ = false;
  loop_->queueInLoop(boost::bind(&Connector::stopInLoop, this)); // FIXME: unsafe
  // FIXME: cancel timer
}

void Connector::stopInLoop()
{
  loop_->assertInLoopThread();
  if (state_ == kConnecting)
  {
    setState(kDisconnected);
    // WARNING: currently no support cancel connect request.
    //int err = uv_cancel(reinterpret_cast<uv_req_t*>(req_));
    //LOG_ERROR << uv_strerror(err) << " in Connector::stopInLoop";
    retry();
  }
}

void Connector::connect()
{
  if (nullptr == socket_)
  {
    socket_ = loop_->getFreeTcpSocket();
    if (!socket_)
    {
      LOG_ERROR << " no free socket in Connector::connect";
      retry();
      return;
    }
  }

  ConnectRequest *connectReq = new ConnectRequest;
  connectReq->connector = shared_from_this();
  connectReq->req.data = connectReq;
  int err = uv_tcp_connect(&connectReq->req, socket_, &serverAddr_.getSockAddr(), 
    &Connector::onConnectCallback);
  if (err)
  {
    LOG_SYSERR << uv_strerror(err) << "in Connector::connect";
  }
  handleConnectError(err);
}

void Connector::onConnectCallback( uv_connect_t *req, int status )
{
  assert(req->data);
  ConnectRequest *connectReq = static_cast<ConnectRequest*>(req->data);
  ConnectorPtr connector = connectReq->connector.lock();
  delete connectReq;
  if (!connector) 
  {
    LOG_WARN << "Connector has been destructed before onConnectCallback";
  }
  else
  {
    LOG_TRACE << "Connector::onConnectCallback " << connector->state_;

    if (connector->state_ == Connector::kConnecting) 
    {
      if (status) 
      {
        LOG_SYSERR << uv_strerror(status) << " in Connector::onConnectCallback";
        connector->handleConnectError(status);
      }
      else if (TcpSocket::isSelfConnect(connector->socket_))
      {
        LOG_WARN << "self connect in Connector::onConnectCallback";
        connector->retry();
      }
      else
      {
        connector->setState(kConnected);
        if (connector->connect_)
        {
          connector->newConnectionCallback_(connector->socket_);
        }
        else
        {
          connector->loop_->closeSocketInLoop(connector->socket_);
        }
      }
    }
    else
    {
      assert(connector->state_ == Connector::kDisconnected);
    }
  }
}

void Connector::handleConnectError( int err )
{
  switch (err)
  {
    case 0:
    case UV_EINTR:
    case UV_EISCONN:
      connecting();
      break;

    case UV_EAGAIN:
    case UV_EADDRINUSE:
    case UV_EADDRNOTAVAIL:
    case UV_ECONNREFUSED:
    case UV_ENETUNREACH:
      retry();
      break;

    case UV_EACCES:
    case UV_EPERM:
    case UV_EAFNOSUPPORT:
    case UV_EALREADY:
    case UV_EBADF:
    case UV_EFAULT:
    case UV_ENOTSOCK:
      LOG_SYSERR << "Connect error:" << uv_err_name(err);
      loop_->closeSocketInLoop(socket_);
      socket_ = nullptr;
      break;

    default:
      LOG_SYSERR << "Unexpected error:" << uv_err_name(err);
      loop_->closeSocketInLoop(socket_);
      socket_ = nullptr;
      // connectErrorCallback_();
    break;
  }
}

void Connector::restart()
{
  loop_->assertInLoopThread();
  setState(kDisconnected);
  retryDelayMs_ = kInitRetryDelayMs;
  connect_ = true;
  startInLoop();
}

void Connector::connecting()
{
  setState(kConnecting);
}

void Connector::retry()
{
  if (socket_)
  {
    loop_->closeSocketInLoop(socket_);
    socket_ = nullptr;
  }
  setState(kDisconnected);
  if (connect_)
  {
    LOG_INFO << "Connector::retry - Retry connecting to " << serverAddr_.toIpPort()
             << " in " << retryDelayMs_ << " milliseconds. ";
    loop_->runAfter(retryDelayMs_/1000.0,
                    boost::bind(&Connector::startInLoop, shared_from_this()));
    retryDelayMs_ = (std::min)(retryDelayMs_ * 2, kMaxRetryDelayMs);
  }
  else
  {
    LOG_DEBUG << "do not connect";
  }
}






