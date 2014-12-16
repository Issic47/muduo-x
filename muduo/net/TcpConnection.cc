// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen

#include <muduo/net/TcpConnection.h>

#include <muduo/base/Logging.h>
#include <muduo/base/WeakCallback.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>

#include <boost/bind.hpp>

#include <list>

namespace muduo
{
namespace net
{

class OutputBuffer : boost::noncopyable
{
 public:
  typedef std::list<Buffer> BufferList;

  OutputBuffer()
    : readableBytes_(0)
  {
    outputBuffers_.push_back(Buffer());
    writeBuffer_ = outputBuffers_.begin();
    readBuffer_ = outputBuffers_.begin();
  }

  size_t readableBytes() const { return readableBytes_; }

  char* append(const void *data, size_t len) 
  {
    char* beginWrite = nullptr;
    BufferList::iterator avaliableBuffer = findAvaliableBuffer(len);
    (*avaliableBuffer).ensureWritableBytes(len);
    beginWrite = (*avaliableBuffer).beginWrite();
    (*avaliableBuffer).append(data, len);
    writeBuffer_ = avaliableBuffer;
    readableBytes_ += len;
    return beginWrite;
  }

  void retrieve(size_t len)
  {
    (*readBuffer_).retrieve(len);
    if ((*readBuffer_).readableBytes() == 0)
    {
      readBuffer_ = next(readBuffer_);
    }
    readableBytes_ -= len;
  }

 private:
  BufferList::iterator findAvaliableBuffer(size_t len) 
  {
    if ((*writeBuffer_).writableBytes() >= len || 
        (*writeBuffer_).readableBytes() == 0)
    {
      return writeBuffer_;
    }
    BufferList::iterator nextWriteBuffer = next(writeBuffer_);
    if ((*nextWriteBuffer).readableBytes() == 0)
    {
      return nextWriteBuffer;
    }
    else
    {
      return outputBuffers_.insert(nextWriteBuffer, Buffer(len));
    }
  }

  BufferList::iterator next(BufferList::iterator it)
  {
    BufferList::iterator nextIt = ++it;
    return nextIt == outputBuffers_.end() ? outputBuffers_.begin() : nextIt;
  }

 private:
  BufferList::iterator readBuffer_;
  BufferList::iterator writeBuffer_;

  size_t readableBytes_;
  BufferList outputBuffers_;
  
};

} // namespace net
} // namespace muduo


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

TcpConnection::TcpConnection(const string& nameArg,
                             uv_tcp_t *socket,
                             const InetAddress& localAddr,
                             const InetAddress& peerAddr)
  : loop_(nullptr),
    name_(nameArg),
    state_(kConnecting),
    socket_(new Socket(CHECK_NOTNULL(socket))),
    localAddr_(localAddr),
    peerAddr_(peerAddr),
    highWaterMark_(64*1024*1024),
    isClosing_(false),
    outputBuffer_(new OutputBuffer)
{
  assert(socket->loop);
  assert(socket->loop->data);
  loop_ = static_cast<EventLoop*>(socket->loop->data);
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
  loop_->closeSocketInLoop(socket_->socket());
  releaseFreeWriteReq();
}

void TcpConnection::releaseFreeWriteReq()
{
  while (!freeWriteReqList_.empty())
  {
    WriteRequest *req = freeWriteReqList_.front();
    freeWriteReqList_.pop_front();
    delete req;
  }
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

  if (socket_->getWriteQueueSize() == 0 && outputBuffer_->readableBytes() == 0) 
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
    else
    {
      int err = nwrote;
      nwrote = 0;
      if (err != UV_EAGAIN && err != UV_ENOSYS)
      {
        LOG_SYSERR << uv_strerror(err) << " in TcpConnection::SendInLoop";
        if (err == UV_EPIPE || err == UV_ECONNRESET)
        {
          faultError = true;
        }
      }
    }
  }

  assert(remaining <= len);

  if (!faultError && remaining > 0)
  {
    size_t oldLen = outputBuffer_->readableBytes();
    if (oldLen + remaining >= highWaterMark_ && 
        oldLen < highWaterMark_ && 
        highWaterMarkCallback_)
    {
      loop_->queueInLoop(boost::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
    }
    
    WriteRequest *writeReq = getFreeWriteReq();
    writeReq->req.data = writeReq;
    writeReq->conn = shared_from_this();
    writeReq->buf = uv_buf_init(
      outputBuffer_->append(static_cast<const char*>(data)+nwrote, remaining),
      remaining);
    int err = socket_->write(&writeReq->req, &writeReq->buf, 1, &TcpConnection::writeCallback);
    if (err)
    {
      LOG_SYSFATAL << uv_strerror(err) << " in TcpConnection::sendInLoop";
    }
  }
}


void TcpConnection::writeCallback( uv_write_t *handle, int status )
{
  if (status)
  {
    LOG_SYSERR << uv_strerror(status) << " in TcpConnection::writeCallback";
  }
  else
  {
    assert(handle->data);
    WriteRequest *writeReq = static_cast<WriteRequest*>(handle->data);
    TcpConnectionPtr conn = writeReq->conn.lock();
    if (conn)
    {
      conn->outputBuffer_->retrieve(writeReq->buf.len);
      conn->collectFreeWriteReq(writeReq);
      if (conn->writeCompleteCallback_)
      {
        conn->loop_->queueInLoop(boost::bind(conn->writeCompleteCallback_, conn));
      }
      if (conn->state_ == kDisconnecting)
      {
        conn->shutdownInLoop();
      }
    }
    else
    {
      LOG_WARN << "TcpConnection has been destructed before writeCallback";
      delete writeReq;
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

  ShutdownRequest *shutdownReq = new ShutdownRequest;
  shutdownReq->conn = shared_from_this();
  shutdownReq->req.data = shutdownReq;
  isClosing_ = false;
  // we are not writing
  socket_->shutdownWrite(&shutdownReq->req, &TcpConnection::shutdownCallback);
}

void TcpConnection::shutdownCallback( uv_shutdown_t *req, int status )
{
  assert(req->data);
  ShutdownRequest *shutdownRequest = static_cast<ShutdownRequest*>(req->data);
  TcpConnectionPtr connection = shutdownRequest->conn.lock();
  delete shutdownRequest;

  if (status) 
  {
    LOG_SYSERR << uv_strerror(status) << " in TcpConnection::shutdownCallback";
  }

  if (connection && connection->isClosing_)
  {
    TcpConnectionPtr guardThis(connection->shared_from_this());
    connection->connectionCallback_(guardThis);
    // must be the last line
    connection->closeCallback_(guardThis);
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
  // FIXME(cbj): resizing buffer would cause buf->base in readCallback crash?
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

void TcpConnection::disableReadWrite(bool closeAfterDisable)
{
  int err = socket_->readStop();
  if (err)
  {
    LOG_ERROR << uv_strerror(err) << " in TcpConnection::disableReadWrite";
  }

  ShutdownRequest *shutdownReq = new ShutdownRequest;
  shutdownReq->conn = shared_from_this();
  shutdownReq->req.data = shutdownReq;
  isClosing_ = closeAfterDisable;
  socket_->shutdownWrite(&shutdownReq->req, &TcpConnection::shutdownCallback);
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

void TcpConnection::handleError(int err)
{
  //int err = sockets::getSocketError(channel_->fd());
  LOG_ERROR << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << uv_err_name(err) << " " << uv_strerror(err);
}
