#include <muduo/net/UdpSocket.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Buffer.h>

#include <boost/bind.hpp>

using namespace muduo;
using namespace muduo::net;


void muduo::net::defaultUdpMessageCallback(const UdpSocketPtr& socket, 
                                           Buffer* buffer, 
                                           const InetAddress& srcAddr, 
                                           Timestamp receiveTimer)
{
  buffer->retrieveAll();
}

UdpSocket::UdpSocket(EventLoop* loop)
  : loop_(CHECK_NOTNULL(loop)),
    inputBuffer_(65536),
    bytesInSend_(0),
    highWaterMark_(64*1024*1024),
    connectModel_(false),
    receiving_(false)
{
  socket_ = loop_->getFreeUdpSocket();
  socket_->data = this;
}

UdpSocket::UdpSocket( EventLoop* loop, const InetAddress& bindAddr, bool reuseAddr )
  : loop_(CHECK_NOTNULL(loop)),
    inputBuffer_(65536),
    bytesInSend_(0),
    highWaterMark_(64*1024*1024),
    connectModel_(false),
    receiving_(false)
{
  socket_ = loop_->getFreeUdpSocket();
  socket_->data = this;
  bind(bindAddr, reuseAddr);
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

int UdpSocket::send( const StringPiece& message )
{
  assert(connectModel_);
  return send(peerAddr_, message);
}

int UdpSocket::send( const void* data, int len )
{
  assert(connectModel_);
  return send(peerAddr_, StringPiece(static_cast<const char*>(data), len));
}

int UdpSocket::send( Buffer* buf )
{
  assert(connectModel_);
  return send(peerAddr_, buf);
}

int UdpSocket::send(const InetAddress& addr, const void* data, int len)
{
  return send(addr, StringPiece(static_cast<const char*>(data), len));
}

int UdpSocket::send(const InetAddress& addr, const StringPiece& message)
{
  int messageId = messageId_.incrementAndGet();
  if (loop_->isInLoopThread())
  {
    sendInLoop(messageId, addr, message);
  }
  else
  {
    loop_->runInLoop(
      boost::bind(&UdpSocket::sendInLoop,
                  this, // FIXME
                  messageId,
                  InetAddress(addr),
                  message.as_string()));
                  //std::forward<string>(message)));
  }
  return messageId;
}

int UdpSocket::send(const InetAddress& addr, Buffer* buf)
{
  int messageId = messageId_.incrementAndGet();
  if (loop_->isInLoopThread())
  {
    sendInLoop(messageId, addr, buf->peek(), buf->readableBytes());
    buf->retrieveAll();
  }
  else
  {
    loop_->runInLoop(
      boost::bind(&UdpSocket::sendInLoop,
                  this, // FIXME
                  messageId,
                  InetAddress(addr),
                  buf->retrieveAllAsString()));
                  //std::forward<string>(message)));
  }
  return messageId;
}

void UdpSocket::sendInLoop(int messageId, const InetAddress& addr, const StringPiece& message)
{
  sendInLoop(messageId, addr, message.data(), message.size());
}

void UdpSocket::sendInLoop(int messageId, const InetAddress& addr, const void* data, size_t len)
{
  loop_->assertInLoopThread();
  uv_buf_t buf = uv_buf_init(static_cast<char*>(const_cast<void*>(data)), static_cast<unsigned int>(len));
  ssize_t nwrite = uv_udp_try_send(socket_, &buf, 1, &addr.getSockAddr());
  bool faultError = false;
  if (nwrite > 0)
  {
    if (len != static_cast<size_t>(nwrite))
    {
      LOG_ERROR << "UDP data send truncated: " << len << "B to " << nwrite << "B";
    }
    if (writeCompleteCallback_)
    {
      loop_->queueInLoop(boost::bind(writeCompleteCallback_, shared_from_this(), messageId));
    }
  }
  else
  {
    int err = static_cast<int>(nwrite);
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
    sendRequest->messageId = messageId;
    uv_buf_t restBuf = uv_buf_init(sendRequest->buf.beginWrite(), static_cast<unsigned int>(len));
    sendRequest->buf.append(data, len);
    int err = uv_udp_send(&sendRequest->req, 
                          socket_, 
                          &restBuf, 
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
    if (socket->writeCompleteCallback_)
    {
      socket->loop_->queueInLoop(
        boost::bind(socket->writeCompleteCallback_, socket, sendRequest->messageId));
    }
    socket->releaseSendReq(sendRequest);
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
    if (startedRecvCallback_)
    {
      loop_->queueInLoop(boost::bind(startedRecvCallback_, shared_from_this()));
    }
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
    LOG_SYSERR << uv_strerror(static_cast<int>(nread)) << " in UdpSocket::recvCallback";  
  }
  else
  {
    if (flag == UV_UDP_PARTIAL)
    {
      LOG_ERROR << "Input buffer is no big enough to hold the UDP package in UdpSocket::recvCallback";
    }
    // TODO: UV_UDP_IPV6ONLY
    if (src == nullptr)
    {
      // There is nothing to read
      assert(nread == 0);
      return;
    }
    else
    {
      InetAddress srcAddress(*src);
      if (socket->connectModel_ && socket->peerAddr_ != srcAddress)
      {
        LOG_INFO << "Ignore UDP data from " << srcAddress.toIpPort();
        return;
      }
      socket->inputBuffer_.hasWritten(nread);
      socket->messageCallback_(
        socket->shared_from_this(),
        &socket->inputBuffer_, 
        srcAddress,
        socket->loop_->pollReturnTime());
    }
  }
}

void UdpSocket::setBroadcast( bool on )
{
  int err = uv_udp_set_broadcast(socket_, on ? 1 : 0);
  if (err)
  {
    LOG_SYSFATAL << uv_strerror(err) << " in UdpSocket::setBroadcast";
  }
}

void UdpSocket::setMulticastLoop( bool on )
{
  int err = uv_udp_set_multicast_loop(socket_, on ? 1 : 0);
  if (err)
  {
    LOG_SYSFATAL << uv_strerror(err) << " in UdpSocket::setMulticastLoop";
  }
}

void UdpSocket::setMulticastTTL( int ttl )
{
  int err = uv_udp_set_multicast_ttl(socket_, ttl);
  if (err)
  {
    LOG_SYSFATAL << uv_strerror(err) << " in UdpSocket::setMulticastTTL";
  }
}

void muduo::net::UdpSocket::setTTL( int ttl )
{
  int err = uv_udp_set_ttl(socket_, ttl);
  if (err)
  {
    LOG_SYSFATAL << uv_strerror(err) << " in UdpSocket::setTTL";
  }
}

void UdpSocket::setMulticastInterface( const string& interfaceAddr )
{
  int err = uv_udp_set_multicast_interface(socket_, interfaceAddr.c_str());
  if (err)
  {
    LOG_SYSFATAL << uv_strerror(err) << " in UdpSocket::setMulticastInterface";
  }
}

void UdpSocket::setMemberShip(const string& multicastAddr,
                              const string& interfaceAddr, 
                              bool join)
{
  int err = uv_udp_set_membership(socket_, 
                                  multicastAddr.c_str(),
                                  interfaceAddr.c_str(),
                                  join ? UV_JOIN_GROUP : UV_LEAVE_GROUP);
  if (err)
  {
    LOG_SYSFATAL << uv_strerror(err) << " in UdpSocket::setMembership";
  }
}

