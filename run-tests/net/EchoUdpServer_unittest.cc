#include <muduo/net/UdpServer.h>

#include <muduo/net/UdpSocket.h>
#include <muduo/net/Buffer.h>
#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include <boost/bind.hpp>

#include <utility>

#include <stdio.h>
//#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

int numThreads = 0;

class EchoServer
{
 public:
  EchoServer(EventLoop* loop, const InetAddress& listenAddr)
    : loop_(loop),
      server_(loop, listenAddr, "EchoServer", UdpServer::kReuseAddr)
  {
    server_.setMessageCallback(
        boost::bind(&EchoServer::onMessage, this, _1, _2, _3, _4));
  }

  void start()
  {
    server_.start();
  }
  // void stop();

 private:
  void onMessage(const UdpSocketPtr& socket, Buffer* buf, const InetAddress& src, Timestamp time)
  {
    string msg(buf->retrieveAllAsString());
    LOG_TRACE << server_.name() << " recv " << msg.size() << " bytes at " << time.toString()
              << " from " << src.toIpPort();
    if (msg == "exit\n")
    {
      socket->send(src, "bye\n");
    }
    if (msg == "quit\n")
    {
      loop_->quit();
    }
    socket->send(src, msg);
  }

  EventLoop* loop_;
  UdpServer server_;
};

int main(int argc, char* argv[])
{
  Logger::setLogLevel(Logger::kTRACE);
  LOG_INFO << "pid = " << getpid() << ", tid = " << CurrentThread::tid();
  LOG_INFO << "sizeof UdpSocket = " << sizeof(UdpSocket);
  EventLoop loop;
  InetAddress listenAddr(AF_INET, 7890);
  printf("hostport: %s\n", listenAddr.toIpPort().c_str());
  EchoServer server(&loop, listenAddr);

  server.start();

  loop.loop();
}

