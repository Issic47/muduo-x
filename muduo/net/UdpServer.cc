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
  : loop_(CHECK_NOTNULL(loop_)),
    hostport_(listenAddr.toIpPort()),
    name_(nameArg),
    socket_(new UdpSocket(loop_, listenAddr, option == kReuseAddr)),
    messageCallback_(defualtMessageCallback)
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
    loop_->runInLoop(boost::bind(&UdpSocket::startRecv, get_pointer(socket_)));
  }
}


