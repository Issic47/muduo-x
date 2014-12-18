#include <muduo/net/UdpServer.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/UdpSocket.h>

#include <boost/bind.hpp>

using namespace muduo;
using namespace muduo::net;

UdpServer::UdpServer(EventLoop* loop, 
                     const InetAddress& listenAddr, 
                     const string& nameArg,
                     Option option /*=kReuseAddr*/)
  : loop_(CHECK_NOTNULL(loop)),
    hostport_(listenAddr.toIpPort()),
    name_(nameArg),
    socket_(new UdpSocket(loop, listenAddr, option == kReuseAddr)),
    messageCallback_(defaultUdpMessageCallback)
{
}

UdpServer::~UdpServer()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "UdpServer::~UdpServer [" << name_ << "] destructing";
}

void UdpServer::start()
{
  if (started_.getAndSet(1) == 0)
  {
    assert(!socket_->receiving());
    socket_->setMessageCallback(messageCallback_);
    socket_->setWriteCompleteCallback(writeCompleteCallback_);
    socket_->setHighWatermarkCallback(highWaterMarkCallback_);
    loop_->runInLoop(boost::bind(&UdpSocket::startRecv, get_pointer(socket_)));
  }
}

void UdpServer::stop()
{
  if (started_.getAndSet(0) == 1)
  {
    assert(socket_->receiving());
    loop_->runInLoop(boost::bind(&UdpSocket::stopRecv, get_pointer(socket_)));
  }
}
