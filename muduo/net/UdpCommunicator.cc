#include <muduo/net/UdpCommunicator.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Buffer.h>

#include <boost/bind.hpp>

using namespace muduo;
using namespace muduo::net;



int UdpCommunicator::send(const InetAddress& addr, const void* data, int len )
{
  send(addr, StringPiece(static_cast<const char*>(data), len));
}

int UdpCommunicator::send(const InetAddress& addr, const StringPiece& message)
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

int UdpCommunicator::send(const InetAddress& addr, Buffer* buf)
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
    sendRequest->buf = uv_buf_init();
    int err = uv_udp_send(&sendRequest->req, 
                          socket_, 
                          &sendRequest->buf, 
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
  if (status)
  {
    LOG_SYSERR << uv_strerror(status) << " in UdpCommnunicator::sendCallback";
  }
  else
  {
    assert(req->data);
    SendRequest *sendRequest = static_cast<SendRequest*>(req->data);
    UdpCommunicatorPtr communicator = sendRequest->communicator.lock();
    if (communicator)
    {
      communicator->collectSendReq(sendRequest);
      if (communicator->writeCompleteCallback_)
      {
        communicator->writeCompleteCallback_();
      }
    }
  }
}
