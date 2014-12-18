#include <muduo/net/UdpClient.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/UdpSocket.h>

#include <boost/bind.hpp>

using namespace muduo;
using namespace muduo::net;


UdpClient::UdpClient(EventLoop* loop, const InetAddress& serverAddr, const string& name )
  : loop_(CHECK_NOTNULL(loop)),
    name_(name),
    socket_(new UdpSocket(loop)),
    messageCallback_(defaultUdpMessageCallback),
    peerAddr_(serverAddr)
{
  socket_->connect(peerAddr_);
}

UdpClient::~UdpClient()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "UdpClient::~UdpClient [" << name_ << "] destructing";
}

void UdpClient::connect()
{
  if (connect_.getAndSet(1) == 0)
  {
    LOG_INFO << "UdpClient::connect[" << name_ << "] - connecting to "
             << peerAddr_.toIpPort();
    assert(!socket_->receiving());  
    socket_->setMessageCallback(messageCallback_);
    socket_->setWriteCompleteCallback(writeCompleteCallback_);
    socket_->setHighWatermarkCallback(highWaterMarkCallback_);
    socket_->setStartedRecvCallback(startedRecvCallback_);
    loop_->runInLoop(boost::bind(&UdpSocket::startRecv, get_pointer(socket_)));
  }
}

void UdpClient::disconnect()
{
  if (connect_.getAndSet(0) == 1)
  {
    LOG_INFO << "UdpClient::disconnect[" << name_ << "] - disconnect from "
             << peerAddr_.toIpPort();
    assert(socket_->receiving());
    loop_->runInLoop(boost::bind(&UdpSocket::stopRecv, get_pointer(socket_)));
  }
}




