// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/TcpConnection.h>

#include <muduo/base/Logging.h>
#include <muduo/base/WeakCallback.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

void muduo::net::defaultConnectionCallback(const TcpConnectionPtr& conn)
{
  LOG_TRACE << conn->localAddress().toIpPort() << " -> "
            << conn->peerAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
  // do not call conn->forceClose(), because some users want to register message callback only.
}

void muduo::net::defaultMessageCallback(const TcpConnectionPtr&,
                                        Buffer* buf,
                                        Timestamp)
{
  buf->retrieveAll();
}

TcpConnection::TcpConnection(EventLoop* loop,
                             const string& nameArg,
                             uv_tcp_t *socket,
                             const InetAddress& localAddr,
                             const InetAddress& peerAddr)
  : loop_(CHECK_NOTNULL(loop)),
    name_(nameArg),
    state_(kConnecting),
    socket_(new Socket(socket)),
    localAddr_(localAddr),
    peerAddr_(peerAddr),
    highWaterMark_(64*1024*1024),
    isClosing_(false)
{
  LOG_DEBUG << "TcpConnection::ctor[" <<  name_ << "] at " << this
            << " fd=" << socket_->fd();
  socket_->setData(this);
  socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
  LOG_DEBUG << "TcpConnection::dtor[" <<  name_ << "] at " << this
            << " fd=" << socket_->fd()
            << " state=" << state_;
  assert(state_ == kDisconnected);
}

bool TcpConnection::getTcpInfo(struct tcp_info* tcpi) const
{
  return socket_->getTcpInfo(tcpi);
}

string TcpConnection::getTcpInfoString() const
{
  char buf[1024];
  buf[0] = '\0';
  socket_->getTcpInfoString(buf, sizeof buf);
  return buf;
}

void TcpConnection::send(const void* data, int len)
{
  send(StringPiece(static_cast<const char*>(data), len));
}

void TcpConnection::send(const StringPiece& message)
{
  if (state_ == kConnected)
  {
    if (loop_->isInLoopThread())
    {
      sendInLoop(message);
    }
    else
    {
      loop_->runInLoop(
          boost::bind(&TcpConnection::sendInLoop,
                      this,     // FIXME
                      message.as_string()));
                    //std::forward<string>(message)));
    }
  }
}

// FIXME efficiency!!!
void TcpConnection::send(Buffer* buf)
{
  if (state_ == kConnected)
  {
    if (loop_->isInLoopThread())
    {
      sendInLoop(buf->peek(), buf->readableBytes());
      buf->retrieveAll();
    }
    else
    {
      loop_->runInLoop(
          boost::bind(&TcpConnection::sendInLoop,
                      this,     // FIXME
                      buf->retrieveAllAsString()));
                    //std::forward<string>(message)));
    }
  }
}

void TcpConnection::sendInLoop(const StringPiece& message)
{
  sendInLoop(message.data(), message.size());
}

void TcpConnection::sendInLoop(const void* data, size_t len)
{
  loop_->assertInLoopThread();
  ssize_t nwrote = 0;
  size_t remaining = len;
  bool faultError = false;
  if (state_ == kDisconnected)
  {
    LOG_WARN << "disconnected, give up writing";
    return;
  }

  if (socket_->getWriteQueueSize() == 0 && outputBuffer_.readableBytes() == 0) 
  {
    uv_buf_t buf = uv_buf_init(
      static_cast<char*>(const_cast<void*>(data)), 
      len);
    nwrote = socket_->tryWrite(&buf, 1);
    if (nwrote >= 0)
    {
      remaining = len - nwrote;
      if (remaining == 0 && writeCompleteCallback_) 
      {
        loop_->queueInLoop(boost::bind(writeCompleteCallback_, shared_from_this()));
      }
    }
    else if (nwrote != UV_EAGAIN)
    {
      LOG_SYSERR << uv_strerror(nwrote) << " in TcpConnection::SendInLoop";
      if (nwrote == UV_EPIPE || nwrote == UV_ECONNRESET)
      {
        faultError = true;
      }
    }
  }

  assert(remaining <= len);

  if (!faultError && remaining > 0)
  {
    size_t oldLen = outputBuffer_.readableBytes();
    if (oldLen + remaining >= highWaterMark_ && 
        oldLen < highWaterMark_ && 
        highWaterMarkCallback_)
    {
      loop_->queueInLoop(boost::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
    }
    
    outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);
    uv_buf_t buf = uv_buf_init(outputBuffer_.beginWrite(), remaining);
    
  }

  assert(remaining <= len);
  if (!faultError && remaining > 0)
  {
    size_t oldLen = outputBuffer_.readableBytes();
    if (oldLen + remaining >= highWaterMark_
        && oldLen < highWaterMark_
        && highWaterMarkCallback_)
    {
      loop_->queueInLoop(boost::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
    }
    outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);
    if (!channel_->isWriting())
    {
      channel_->enableWriting();
    }
  }
}

void TcpConnection::shutdown()
{
  // FIXME: use compare and swap
  if (state_ == kConnected)
  {
    setState(kDisconnecting);
    // FIXME: shared_from_this()?
    loop_->runInLoop(boost::bind(&TcpConnection::shutdownInLoop, this));
  }
}

void TcpConnection::shutdownInLoop()
{
  loop_->assertInLoopThread();

  uv_shutdown_t *req = new uv_shutdown_t;
  req->data = this;
  isClosing_ = false;
  // we are not writing
  socket_->shutdownWrite(req, &TcpConnection::shutdownCallback);
}

void TcpConnection::shutdownCallback( uv_shutdown_t *req, int status )
{
  assert(req->data);
  TcpConnection *connect = reinterpret_cast<TcpConnection*>(req->data);
  delete req;

  if (status) 
  {
    LOG_SYSERR << uv_strerror(status) << " in TcpConnection::shutdownCallback";
    // FIXME(cbj): return?
    //return;
  }

  if (connect->isClosing_)
  {
    TcpConnectionPtr guardThis(connect->shared_from_this());
    connect->connectionCallback_(guardThis);
    // must be the last line
    connect->closeCallback_(guardThis);
  }
}


// void TcpConnection::shutdownAndForceCloseAfter(double seconds)
// {
//   // FIXME: use compare and swap
//   if (state_ == kConnected)
//   {
//     setState(kDisconnecting);
//     loop_->runInLoop(boost::bind(&TcpConnection::shutdownAndForceCloseInLoop, this, seconds));
//   }
// }

// void TcpConnection::shutdownAndForceCloseInLoop(double seconds)
// {
//   loop_->assertInLoopThread();
//   if (!channel_->isWriting())
//   {
//     // we are not writing
//     socket_->shutdownWrite();
//   }
//   loop_->runAfter(
//       seconds,
//       makeWeakCallback(shared_from_this(),
//                        &TcpConnection::forceCloseInLoop));
// }

void TcpConnection::forceClose()
{
  // FIXME: use compare and swap
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    setState(kDisconnecting);
    loop_->queueInLoop(boost::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
  }
}

void TcpConnection::forceCloseWithDelay(double seconds)
{
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    setState(kDisconnecting);
    loop_->runAfter(
        seconds,
        makeWeakCallback(shared_from_this(),
                         &TcpConnection::forceClose));  // not forceCloseInLoop to avoid race condition
  }
}

void TcpConnection::forceCloseInLoop()
{
  loop_->assertInLoopThread();
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    // as if we received 0 byte in handleRead();
    handleClose();
  }
}

void TcpConnection::setTcpNoDelay(bool on)
{
  socket_->setTcpNoDelay(on);
}

void TcpConnection::connectEstablished()
{
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);
  setState(kConnected);

  
  socket_->setData(this);
  int err = socket_->readStart(&TcpConnection::allocCallback,
                               &TcpConnection::readCallback);
  if (err) 
  {
    LOG_SYSERR << uv_strerror(err) << " in TcpConnection::connectEstablished";
  }

  connectionCallback_(shared_from_this());
}

void TcpConnection::allocCallback( uv_handle_t *handle, size_t suggestedSize, uv_buf_t *buf )
{
  assert(handle->data);
  TcpConnection *connetion = static_cast<TcpConnection*>(handle->data);
  connetion->inputBuffer_.ensureWritableBytes(suggestedSize);
  buf->base = connetion->inputBuffer_.beginWrite();
  buf->len = suggestedSize;
}

void TcpConnection::readCallback( uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf )
{
  assert(handle->data);
  TcpConnection *connection = static_cast<TcpConnection*>(handle->data);
  if (nread < 0)
  {
    LOG_DEBUG << uv_strerror(nread);
    if (nread == UV_EOF || nread == UV_ECONNRESET)
    {
      connection->handleClose();
    } 
    else 
    {
      LOG_SYSERR << uv_strerror(nread) << " in TcpConnection::readCallback";
      connection->handleError(nread);
    }
  }
  else if (nread > 0)
  {
    connection->inputBuffer_.hasWritten(nread);
    connection->messageCallback_(
      connection->shared_from_this(),
      &connection->inputBuffer_, 
      connection->loop_->pollReturnTime());
  }

}

void TcpConnection::handleClose()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "fd = " << socket_->fd() << " state = " << state_;
  assert(state_ == kConnected || state_ == kDisconnecting);
  // we don't close fd, leave it to dtor, so we can find leaks easily.
  setState(kDisconnected);

  disableReadWrite(true);
}

void muduo::net::TcpConnection::disableReadWrite(bool closeAfterDisable)
{
  int err = socket_->readStop();
  if (err)
  {
    LOG_ERROR << uv_strerror(err) << " in TcpConnection::disableReadWrite";
  }

  uv_shutdown_t *req = new uv_shutdown_t;
  req->data = this;
  isClosing_ = closeAfterDisable;
  socket_->shutdownWrite(req, &TcpConnection::shutdownCallback);
}

void TcpConnection::connectDestroyed()
{
  loop_->assertInLoopThread();
  if (state_ == kConnected)
  {
    setState(kDisconnected);

    disableReadWrite(false);

    connectionCallback_(shared_from_this());
  }
}

void TcpConnection::handleWrite()
{
  loop_->assertInLoopThread();
  if (channel_->isWriting())
  {
    ssize_t n = sockets::write(channel_->fd(),
                               outputBuffer_.peek(),
                               outputBuffer_.readableBytes());
    if (n > 0)
    {
      outputBuffer_.retrieve(n);
      if (outputBuffer_.readableBytes() == 0)
      {
        channel_->disableWriting();
        if (writeCompleteCallback_)
        {
          loop_->queueInLoop(boost::bind(writeCompleteCallback_, shared_from_this()));
        }
        if (state_ == kDisconnecting)
        {
          shutdownInLoop();
        }
      }
    }
    else
    {
      LOG_SYSERR << "TcpConnection::handleWrite";
      // if (state_ == kDisconnecting)
      // {
      //   shutdownInLoop();
      // }
    }
  }
  else
  {
    LOG_TRACE << "Connection fd = " << channel_->fd()
              << " is down, no more writing";
  }
}



void TcpConnection::handleError(int err)
{
  //int err = sockets::getSocketError(channel_->fd());
  LOG_ERROR << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << uv_err_name(err) << " " << strerror_tl(err);
}



