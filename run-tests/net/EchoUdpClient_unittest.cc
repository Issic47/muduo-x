#include <muduo/net/UdpClient.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Thread.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/UdpSocket.h>
#include <muduo/net/Buffer.h>
#include <boost/bind.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

#include <utility>

#include <stdio.h>
//#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

int numThreads = 0;
class EchoClient;
boost::ptr_vector<EchoClient> clients;
int current = 0;

class EchoClient : boost::noncopyable
{
 public:
  EchoClient(EventLoop* loop, const InetAddress& listenAddr, const string& id)
    : loop_(loop),
      client_(loop, listenAddr, "EchoClient"+id)
  {
    client_.setMessageCallback(
        boost::bind(&EchoClient::onMessage, this, _1, _2, _3, _4));
    client_.setStartedRecvCallback(
        boost::bind(&EchoClient::onStartedRecv, this, _1));
    client_.setWriteCompleteCallback(
        boost::bind(&EchoClient::onWriteComplete, this, _1, _2));
  }

  void connect()
  {
    client_.connect();
  }
  // void stop();

 private:
  void onStartedRecv(const UdpSocketPtr& socket)
  {
    LOG_TRACE << socket->getLocalAddr().toIpPort() << " started receiving data from " 
              << socket->getPeerAddr().toIpPort();
    ++current;
    if (implicit_cast<size_t>(current) < clients.size())
    {
      clients[current].connect();
    }
    LOG_INFO << "*** connected " << current;

    socket->send("world\n");
  }

  void onMessage(const UdpSocketPtr& socket, Buffer* buf, const InetAddress &src, Timestamp time)
  {
    string msg(buf->retrieveAllAsString());
    LOG_TRACE << client_.name() << " recv " << msg.size() << " bytes at " << time.toString();
    if (msg == "quit\n")
    {
      socket->send("bye\n");
      socket->stopRecv();
    }
    else if (msg == "shutdown\n")
    {
      loop_->quit();
    }
    else
    {
      socket->send(msg);
    }
  }

  void onWriteComplete(const UdpSocketPtr& socket, int messageId)
  {
    LOG_TRACE << "message " << messageId << " write completed";
  }

  EventLoop* loop_;
  UdpClient client_;
};

int main(int argc, char* argv[])
{
  Logger::setLogLevel(Logger::kTRACE);
  LOG_INFO << "pid = " << getpid() << ", tid = " << CurrentThread::tid();
  if (argc > 1)
  {
    EventLoop loop;
    InetAddress serverAddr(AF_INET, argv[1], 7890);

    int n = 1;
    if (argc > 2)
    {
      n = atoi(argv[2]);
    }

    clients.reserve(n);
    for (int i = 0; i < n; ++i)
    {
      char buf[32];
      snprintf(buf, sizeof buf, "%d", i+1);
      clients.push_back(new EchoClient(&loop, serverAddr, buf));
    }

    clients[current].connect();
    loop.loop();
  }
  else
  {
    printf("Usage: %s host_ip [current#]\n", argv[0]);
  }
}

