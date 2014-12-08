// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include <muduo/net/Connector.h>

#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <uv.h>

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

const int Connector::kMaxRetryDelayMs;

Connector::Connector(EventLoop* loop, const InetAddress& serverAddr)
  : loop_(loop),
    serverAddr_(serverAddr),
    connect_(false),
    state_(kDisconnected),
    retryDelayMs_(kInitRetryDelayMs),
    socket_(new uv_tcp_t),
    req_(new uv_connect_t)
{
  LOG_DEBUG << "ctor[" << this << "]";
  req_->data = this;
}

Connector::~Connector()
{
  LOG_DEBUG << "dtor[" << this << "]";
  //assert(!channel_);
  if (socket_)
  {
    uv_close(reinterpret_cast<uv_handle_t*>(socket_), 
      &Connector::onHandleCloseCallback);
  }
}

void Connector::onHandleCloseCallback( uv_handle_t *handle )
{
  assert(uv_is_closing(handle));
  assert(handle->data);
  static_cast<Connector*>(handle->data)->socket_ = nullptr;
  delete handle;
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
    int sockfd = removeAndResetChannel();
    retry(sockfd);
  }
}

void Connector::connect()
{
  if (nullptr == socket_)
    socket_ = new uv_tcp_t;

  int err = uv_tcp_init(loop_->getUVLoop(), socket_);
  socket_->data = this;
  if (err) 
  {
    delete socket_;
    socket_ = nullptr;
    LOG_SYSFATAL << uv_strerror(err) << " in Connector::connect";
  }

  err = uv_tcp_connect(req_, socket_, &serverAddr_.getSockAddr(), 
    &Connector::onConnectCallback);
  if (err)
  {
    LOG_ERROR << uv_strerror(err) << "in Connector::connect";
    handleConnectError(err);
  }
}

void Connector::handleConnectError( int err )
{
  switch (err)
  {
  case 0:
  case UV_EINTR:
  case UV_EISCONN:
    connecting(sockfd);
    break;

  case UV_EAGAIN:
  case UV_EADDRINUSE:
  case UV_EADDRNOTAVAIL:
  case UV_ECONNREFUSED:
  case UV_ENETUNREACH:
    retry(sockfd);
    break;

  case UV_EACCES:
  case UV_EPERM:
  case UV_EAFNOSUPPORT:
  case UV_EALREADY:
  case UV_EBADF:
  case UV_EFAULT:
  case UV_ENOTSOCK:
    LOG_SYSERR << "connect error in Connector::startInLoop " << savedErrno;
    sockets::close(sockfd);
    break;

  default:
    LOG_SYSERR << "Unexpected error in Connector::startInLoop " << savedErrno;
    sockets::close(sockfd);
    // connectErrorCallback_();
    break;
  }
}

void Connector::onConnectCallback( uv_connect_t *req, int status )
{
  assert(req->data);
  Connector *connector = static_cast<Connector*>(req->data);
  if (status) 
  {
    LOG_SYSERR << uv_strerror(status) << " in Connector::onConnectCallback";
    connector->handleConnectError(status);
    return;
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

void Connector::connecting(int sockfd)
{
  setState(kConnecting);
  assert(!channel_);
  channel_.reset(new Channel(loop_, sockfd));
  channel_->setWriteCallback(
      boost::bind(&Connector::handleWrite, this)); // FIXME: unsafe
  channel_->setErrorCallback(
      boost::bind(&Connector::handleError, this)); // FIXME: unsafe

  // channel_->tie(shared_from_this()); is not working,
  // as channel_ is not managed by shared_ptr
  channel_->enableWriting();
}

int Connector::removeAndResetChannel()
{
  channel_->disableAll();
  channel_->remove();
  int sockfd = channel_->fd();
  // Can't reset channel_ here, because we are inside Channel::handleEvent
  loop_->queueInLoop(boost::bind(&Connector::resetChannel, this)); // FIXME: unsafe
  return sockfd;
}

void Connector::resetChannel()
{
  channel_.reset();
}

void Connector::handleWrite()
{
  LOG_TRACE << "Connector::handleWrite " << state_;

  if (state_ == kConnecting)
  {
    int sockfd = removeAndResetChannel();
    int err = sockets::getSocketError(sockfd);
    if (err)
    {
      LOG_WARN << "Connector::handleWrite - SO_ERROR = "
               << err << " " << strerror_tl(err);
      retry(sockfd);
    }
    else if (sockets::isSelfConnect(sockfd))
    {
      LOG_WARN << "Connector::handleWrite - Self connect";
      retry(sockfd);
    }
    else
    {
      setState(kConnected);
      if (connect_)
      {
        newConnectionCallback_(sockfd);
      }
      else
      {
        sockets::close(sockfd);
      }
    }
  }
  else
  {
    // what happened?
    assert(state_ == kDisconnected);
  }
}

void Connector::handleError()
{
  LOG_ERROR << "Connector::handleError state=" << state_;
  if (state_ == kConnecting)
  {
    int sockfd = removeAndResetChannel();
    int err = sockets::getSocketError(sockfd);
    LOG_TRACE << "SO_ERROR = " << err << " " << strerror_tl(err);
    retry(sockfd);
  }
}

void Connector::retry(int sockfd)
{
  sockets::close(sockfd);
  setState(kDisconnected);
  if (connect_)
  {
    LOG_INFO << "Connector::retry - Retry connecting to " << serverAddr_.toIpPort()
             << " in " << retryDelayMs_ << " milliseconds. ";
    loop_->runAfter(retryDelayMs_/1000.0,
                    boost::bind(&Connector::startInLoop, shared_from_this()));
    retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
  }
  else
  {
    LOG_DEBUG << "do not connect";
  }
}






