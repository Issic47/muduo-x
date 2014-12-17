#include <muduo/net/UdpSocket.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Buffer.h>

#include <boost/bind.hpp>

using namespace muduo;
using namespace muduo::net;

UdpSocket::UdpSocket( EventLoop* loop, const string& nameArg )
  : loop_(CHECK_NOTNULL(loop)),
    name_(nameArg),
    bytesInSend_(0),
    highWaterMark_(64*1024*1024),
    inputBuffer_(65536),
    receiving_(false)
{
  socket_ = loop_->getFreeUdpSocket();
  socket_->data = this;
}

UdpSocket::~UdpSocket()
{
  socket_->data = nullptr;
  //stopRecv();
  loop_->closeSocketInLoop(socket_);
  releaseAllSendReq();
}

void UdpSocket::releaseAllSendReq()
{
  while (!freeSendReqList_.empty())
  {
    SendRequest *req = freeSendReqList_.front();
    freeSendReqList_.pop_front();
    delete req;
  }
}

InetAddress UdpSocket::getLocalAddr() const
{
  struct sa localAddr;
  int len = sizeof localAddr;
  int err = uv_udp_getsockname(socket_, &localAddr.u.sa, &len);
  if (err)
  {
    LOG_SYSERR << uv_strerror(err) << " in UdpSocket::getLocalAddr";
  }
  return localAddr;
}

void UdpSocket::bind( const InetAddress &addr, bool reuseAddr )
{
  int err = uv_udp_bind(socket_, &addr.getSockAddr(), reuseAddr ? UV_UDP_REUSEADDR : 0);
  if (err)
  {
    LOG_SYSFATAL << uv_strerror(err) << " in UdpSocket::bind";
  }
}

void UdpSocket::send(const InetAddress& addr, const void* data, int len)
{
  send(addr, StringPiece(static_cast<const char*>(data), len));
}

void UdpSocket::send(const InetAddress& addr, const StringPiece& message)
{
  if (loop_->isInLoopThread())
  {
    sendInLoop(addr, message);
  }
  else
  {
    loop_->runInLoop(
      boost::bind(&UdpSocket::sendInLoop,
                  this, // FIXME
                  InetAddress(addr),
                  message.as_string()));
                  //std::forward<string>(message)));
  }
}

void UdpSocket::send(const InetAddress& addr, Buffer* buf)
{
  if (loop_->isInLoopThread())
  {
    sendInLoop(addr, buf->peek(), buf->readableBytes());
    buf->retrieveAll();
  }
  else
  {
    loop_->runInLoop(
      boost::bind(&UdpSocket::sendInLoop,
                  this, // FIXME
                  InetAddress(addr),
                  buf->retrieveAllAsString()));
                  //std::forward<string>(message)));
  }
}

void UdpSocket::sendInLoop(const InetAddress& addr, const StringPiece& message)
{
  sendInLoop(addr, message.data(), message.size());
}

void UdpSocket::sendInLoop(const InetAddress& addr, const void* data, size_t len)
{
  loop_->assertInLoopThread();
  uv_buf_t buf = uv_buf_init(static_cast<char*>(const_cast<void*>(data)), len);
  ssize_t nwrite = uv_udp_try_send(socket_, &buf, 1, &addr.getSockAddr());
  bool faultError = false;
  if (nwrite > 0)
  {
    if (len != nwrite)
    {
      LOG_ERROR << "UDP data send truncated: " << len << "B to " << nwrite << "B";
    }
    if (writeCompleteCallback_)
    {
      loop_->queueInLoop(boost::bind(writeCompleteCallback_, shared_from_this()));
    }
  }
  else
  {
    int err = nwrite;
    nwrite = 0;
    if (err != UV_ENOSYS && err != UV_EAGAIN)
    {
      LOG_SYSERR << uv_strerror(err) << " in UdpSocket::sendInLoop";
      faultError = true;
    }
  }

  if (!faultError && nwrite == 0)
  {
    if (bytesInSend_ + len >= highWaterMark_ &&
        bytesInSend_ < highWaterMark_ &&
        highWaterMarkCallback_)
    {
      loop_->queueInLoop(boost::bind(highWaterMarkCallback_, shared_from_this(), bytesInSend_ + len));
    }
    bytesInSend_ += len;
    SendRequest *sendRequest = getFreeSendReq();
    assert(sendRequest);
    sendRequest->socket = shared_from_this();
    sendRequest->req.data = sendRequest;
    sendRequest->buf.ensureWritableBytes(len);
    uv_buf_t buf = uv_buf_init(sendRequest->buf.beginWrite(), len);
    sendRequest->buf.append(data, len);
    int err = uv_udp_send(&sendRequest->req, 
                          socket_, 
                          &buf, 
                          1,
                          &addr.getSockAddr(),
                          &UdpSocket::sendCallback);
    if (err)
    {
      LOG_SYSFATAL << uv_strerror(err) << " in UdpSocket::sendInLoop";
    }
  }
}

void UdpSocket::sendCallback( uv_udp_send_t *req, int status )
{
  assert(req->data);
  SendRequest *sendRequest = static_cast<SendRequest*>(req->data);
  UdpSocketPtr socket = sendRequest->socket.lock();

  if (status)
  {
    LOG_SYSERR << uv_strerror(status) << " in UdpSocket::sendCallback";
  }

  if (socket)
  {
    socket->bytesInSend_ -= sendRequest->buf.readableBytes();
    sendRequest->buf.retrieveAll();
    socket->releaseSendReq(sendRequest);
    if (socket->writeCompleteCallback_)
    {
      socket->loop_->queueInLoop(
        boost::bind(socket->writeCompleteCallback_, socket));
    }
  }
  else
  {
    LOG_WARN << "UdpSocket has been destructed before writeCallback";
    delete sendRequest;
  }
}

void UdpSocket::startRecv()
{
  loop_->assertInLoopThread();
  if (!receiving_)
  {
    int err = uv_udp_recv_start(
      socket_, &UdpSocket::allocCallback, &UdpSocket::recvCallback);
    if (err && err != UV_EALREADY)
    {
      LOG_SYSFATAL << uv_strerror(err) << " in UdpSocket::startRecv";
    }
    receiving_ = true;
  }
}

void UdpSocket::stopRecv()
{
  loop_->assertInLoopThread();
  int err = uv_udp_recv_stop(socket_);
  if (err)
  {
    LOG_SYSFATAL << uv_strerror(err) << " in UdpSocket::stopRecv";
  }
  receiving_ = false;
}

void UdpSocket::allocCallback( uv_handle_t *handle, size_t suggestedSize, uv_buf_t *buf )
{
  assert(handle->data);
  // FIXME(cbj): not safe
  UdpSocket *socket = static_cast<UdpSocket*>(handle->data);
  assert(socket->inputBuffer_.readableBytes() == 0);
  socket->inputBuffer_.ensureWritableBytes(suggestedSize);
  buf->base = socket->inputBuffer_.beginWrite();
  buf->len = suggestedSize;
}

void UdpSocket::recvCallback(uv_udp_t *handle, 
                             ssize_t nread, 
                             const uv_buf_t *buf,
                             const struct sockaddr *src,
                             unsigned flag)
{
  assert(handle->data);
  UdpSocket *socket = static_cast<UdpSocket*>(handle->data);
  if (nread < 0) 
  {
    LOG_SYSERR << uv_strerror(nread) << " in UdpSocket::recvCallback";  
  }
  else
  {
    socket->inputBuffer_.hasWritten(nread);
    if (flag == UV_UDP_PARTIAL)
    {
      LOG_ERROR << "No enough buffer to hold the UDP package in UdpSocket::recvCallback";
    }
    if (src == nullptr)
    {
      // There is nothing to read
      assert(nread == 0);
      return;
    }
    else
    {
      socket->messageCallback_(
        socket->shared_from_this(),
        &socket->inputBuffer_, 
        InetAddress(*src),
        socket->loop_->pollReturnTime());
    }
  }
}


