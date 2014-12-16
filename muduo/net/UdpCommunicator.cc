#include <muduo/net/UdpCommunicator.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Buffer.h>

#include <boost/bind.hpp>

using namespace muduo;
using namespace muduo::net;

UdpCommunicator::UdpCommunicator( EventLoop* loop, const string& nameArg )
  : loop_(CHECK_NOTNULL(loop)),
    name_(nameArg),
    bytesInSend_(0)
{
}

UdpCommunicator::~UdpCommunicator()
{

}

void UdpCommunicator::bind( const InetAddress &addr, bool reuseAddr )
{
}


void UdpCommunicator::send(const InetAddress& addr, const void* data, int len )
{
  send(addr, StringPiece(static_cast<const char*>(data), len));
}

void UdpCommunicator::send(const InetAddress& addr, const StringPiece& message)
{
  if (loop_->isInLoopThread())
  {
    sendInLoop(addr, message);
  }
  else
  {
    loop_->runInLoop(
      boost::bind(&UdpCommunicator::sendInLoop,
                  this, // FIXME
                  InetAddress(addr),
                  message.as_string()));
                  //std::forward<string>(message)));
  }
}

void UdpCommunicator::send(const InetAddress& addr, Buffer* buf)
{
  if (loop_->isInLoopThread())
  {
    sendInLoop(addr, buf->peek(), buf->readableBytes());
    buf->retrieveAll();
  }
  else
  {
    loop_->runInLoop(
      boost::bind(&UdpCommunicator::sendInLoop,
                  this, // FIXME
                  InetAddress(addr),
                  buf->retrieveAllAsString()));
                  //std::forward<string>(message)));
  }
}

void UdpCommunicator::sendInLoop(const InetAddress& addr, const StringPiece& message)
{
  sendInLoop(addr, message.data(), message.size());
}

void UdpCommunicator::sendInLoop(const InetAddress& addr, const void* data, size_t len)
{
  loop_->assertInLoopThread();
  uv_buf_t buf = uv_buf_init(static_cast<char*>(const_cast<void*>(data)), len);
  ssize_t nwrite = uv_udp_try_send(socket_, &buf, 1, &addr.getSockAddr());
  bool faultError = false;
  if (nwrite > 0)
  {
    if (len != nwrite)
    {
      LOG_ERROR << "UDP data is truncated: " << len << "B to " << nwrite << "B";
    }
    // TODO: call writeComplementCallback?
  }
  else
  {
    int err = nwrite;
    nwrite = 0;
    if (err != UV_ENOSYS && err != UV_EAGAIN)
    {
      LOG_SYSERR << uv_strerror(err) << " in UdpCommnunicator::sendInLoop";
      faultError = true;
    }
  }

  if (!faultError && nwrite == 0)
  {
    SendRequest *sendRequest = getFreeSendReq();
    assert(sendRequest);
    sendRequest->communicator = shared_from_this();
    sendRequest->req.data = sendRequest;
    sendRequest->buf.ensureWritableBytes(len);
    uv_buf_t buf = uv_buf_init(sendRequest->buf.beginWrite(), len);
    sendRequest->buf.append(data, len);
    int err = uv_udp_send(&sendRequest->req, 
                          socket_, 
                          &buf, 
                          1,
                          &addr.getSockAddr(),
                          &UdpCommunicator::sendCallback);
    if (err)
    {
      LOG_SYSFATAL << uv_strerror(err) << " in UdpCommnunicator::sendInLoop";
    }
  }
}

void UdpCommunicator::sendCallback( uv_udp_send_t *req, int status )
{
  assert(req->data);
  SendRequest *sendRequest = static_cast<SendRequest*>(req->data);
  UdpCommunicatorPtr communicator = sendRequest->communicator.lock();

  if (status)
  {
    LOG_SYSERR << uv_strerror(status) << " in UdpCommnunicator::sendCallback";
  }

  if (communicator)
  {
    sendRequest->buf.retrieveAll();
    communicator->releaseSendReq(sendRequest);
    if (communicator->writeCompleteCallback_)
    {
      communicator->writeCompleteCallback_();
    }
  }
  else
  {
    LOG_WARN << "UdpCommunicator has been destructed before writeCallback";
    delete sendRequest;
  }
}



