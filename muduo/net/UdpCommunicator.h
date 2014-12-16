#ifndef MUDUO_NET_UDPCOMMUNICATOR_H
#define MUDUO_NET_UDPCOMMUNICATOR_H

#include <muduo/net/InetAddress.h>
#include <muduo/net/Buffer.h>

#include <muduo/base/Atomic.h>
#include <muduo/base/Types.h>

#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>

#include <list>

namespace muduo
{
namespace net
{

class EventLoop;

///
/// TCP server, supports single-threaded and thread-pool models.
///
/// This is an interface class, so don't expose too much details.
class UdpCommunicator : boost::noncopyable,
                        public boost::enable_shared_from_this<UdpCommunicator>
{
 public:
  UdpCommunicator(EventLoop* loop, const string& nameArg);
  ~UdpCommunicator();

  EventLoop* getLoop() const { return loop_; }
  const string& name() const { return name_; }

  void bind(const InetAddress &addr, bool reuseAddr);

  // TODO: connect

  /// Starts the server if it's not listenning.
  ///
  /// It's harmless to call it multiple times.
  /// Thread safe.
  void startRecv();
  void stopRecv();

  // void send(const InetAddress& peerAddr, string&& message); // C++11
  void send(const InetAddress& addr, const void* message, int len);
  void send(const InetAddress& addr, const StringPiece& message);
  // void send(const InetAddress& peerAddr, Buffer&& message); // C++11
  void send(const InetAddress& addr, Buffer* message);  // this one will swap data

  /// Set message callback.
  /// Not thread safe.
  void setMessageCallback(const MessageCallback& cb)
  { messageCallback_ = cb; }

  /// Set write complete callback.
  /// Not thread safe.
  void setWriteCompleteCallback(const WriteCompleteCallback& cb)
  { writeCompleteCallback_ = cb; }

 private:
  typedef struct SendRequest
  {
    boost::weak_ptr<UdpCommunicator> communicator;
    uv_udp_send_t req;
    Buffer buf;
  } SendRequest;

  static void allocCallback(uv_udp_t *handle, size_t suggestedSize, uv_buf_t *buf);
  static void recvCallback(uv_udp_t *handle, 
                           size_t nread, 
                           const uv_buf_t *buf, 
                           const struct sockaddr *src, 
                           unsigned flag);
  static void sendCallback(uv_udp_send_t *req, int status);
  static void closeCallback(uv_handle_t *handle, int status);
  // TODO: highwatermark

  void sendInLoop(const InetAddress &addr, const StringPiece& message);
  void sendInLoop(const InetAddress &addr, const void* message, size_t len);

  inline SendRequest* getFreeSendReq();
  inline void releaseSendReq(SendRequest *req);

  EventLoop* loop_;  // the acceptor loop
  uv_udp_t *socket_;
  Buffer inputBuffer_;
  const string hostport_;
  const string name_;
  InetAddress localAddr_;
  std::list<SendRequest*> freeSendReqList_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  size_t bytesInSend_;
  AtomicInt32 started_;
  AtomicInt32 messageId_;
};

typedef boost::shared_ptr<UdpCommunicator> UdpCommunicatorPtr;

UdpCommunicator::SendRequest* UdpCommunicator::getFreeSendReq()
{
  if (!freeSendReqList_.empty())
  {
    SendRequest *req = freeSendReqList_.front();
    freeSendReqList_.pop_front();
    return req;
  }
  else
  {
    return new SendRequest;
  }
}

void UdpCommunicator::releaseSendReq(SendRequest *req)
{
  freeSendReqList_.push_back(req);
}


} // namespace net
} // namespace muduo

#endif // MUDUO_NET_UDPCOMMUNICATOR_H
