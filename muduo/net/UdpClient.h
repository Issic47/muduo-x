#ifndef MUDUO_NET_UDPCLIENT_H
#define MUDUO_NET_UDPCLIENT_H

#include <muduo/base/Types.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Atomic.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/InetAddress.h>

#include <boost/noncopyable.hpp>

namespace muduo
{
namespace net
{

class EventLoop;

class UdpClient : boost::noncopyable
{
public:
  UdpClient(EventLoop* loop,
            const InetAddress& serverAddr,
            const string& name);
  ~UdpClient();

  void connect();
  void disconnect();

  UdpSocketPtr socket() const { return socket_; }
  EventLoop* getLoop() const { return loop_; }
  const string& name() const { return name_; }
  bool isConnected() { return connect_.get() == 1; }

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

  void setHighWaterMarkCallback(const UdpHighWaterMarkCallback& cb)
  { highWaterMarkCallback_ = cb; }

  void setHighWaterMarkCallback(UdpHighWaterMarkCallback&& cb)
  { highWaterMarkCallback_ = std::move(cb); }

  void setStartedRecvCallback(const UdpStartedRecvCallback& cb)
  { startedRecvCallback_ = cb; }

  void setStartedRecvCallback(UdpStartedRecvCallback&& cb)
  { startedRecvCallback_ = std::move(cb); }

private:
  EventLoop* loop_;
  const string name_;
  InetAddress peerAddr_;
  UdpMessageCallback messageCallback_;
  UdpWriteCompleteCallback writeCompleteCallback_;
  UdpHighWaterMarkCallback highWaterMarkCallback_;
  UdpStartedRecvCallback startedRecvCallback_;
  AtomicInt32 connect_;
  UdpSocketPtr socket_;
};

}
}

#endif // MUDUO_NET_UDPCLIENT_H
