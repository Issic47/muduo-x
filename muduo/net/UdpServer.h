#ifndef MUDUO_NET_UDP_SERVER_H
#define MUDUO_NET_UDP_SERVER_H

#include <muduo/base/Atomic.h>
#include <muduo/base/Types.h>
#include <muduo/net/Callbacks.h>

#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

namespace muduo
{
namespace net
{

class UdpSocket;
class EventLoop;

  ///
/// TCP server, supports single-threaded and thread-pool models.
///
/// This is an interface class, so don't expose too much details.
class UdpServer : boost::noncopyable
{
 public:
  typedef boost::function<void(EventLoop*)> ThreadInitCallback;
  enum Option
  {
    kNoReuseAddr,
    kReuseAddr,
  };

  //TcpServer(EventLoop* loop, const InetAddress& listenAddr);
  UdpServer(EventLoop* loop,
            const InetAddress& listenAddr,
            const string& nameArg,
            Option option = kReuseAddr);
  ~UdpServer();  // force out-line dtor, for scoped_ptr members.

  const string& hostport() const { return hostport_; }
  const string& name() const { return name_; }
  EventLoop* getLoop() const { return loop_; }

  /// Starts the server if it's not listenning.
  ///
  /// It's harmless to call it multiple times.
  /// Thread safe.
  void start();

  /// Set message callback.
  /// Not thread safe.
  void setMessageCallback(const UdpMessageCallback& cb)
  { messageCallback_ = cb; }
  void setMessageCallback(UdpMessageCallback&& cb)
  { messageCallback_ = std::move(cb); }

  /// Set write complete callback.
  /// Not thread safe.
  void setWriteCompleteCallback(const UdpWriteCompleteCallback& cb)
  { writeCompleteCallback_ = cb; }
  void setWriteCompleteCallback(UdpWriteCompleteCallback&& cb)
  { writeCompleteCallback_ = std::move(cb); }

 private:
  EventLoop* loop_;  // the acceptor loop
  const string hostport_;
  const string name_;
  boost::scoped_ptr<UdpSocket> socket_;
  UdpMessageCallback messageCallback_;
  UdpWriteCompleteCallback writeCompleteCallback_;
  ThreadInitCallback threadInitCallback_;
  AtomicInt32 started_;
};


} // namespace net
} // namespace muduo


#endif // MUDUO_NET_UDP_SERVER_H