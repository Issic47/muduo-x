#ifndef MUDUO_NET_UDPSOCKET_H
#define MUDUO_NET_UDPSOCKET_H

#include <muduo/base/Atomic.h>
#include <muduo/base/Types.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/Callbacks.h>

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
class UdpSocket : boost::noncopyable,
                  public boost::enable_shared_from_this<UdpSocket>
{
 public:
  UdpSocket(EventLoop* loop);
  UdpSocket(EventLoop* loop, const InetAddress& bindAddr, bool reuseAddr);
  ~UdpSocket();

  EventLoop* getLoop() const { return loop_; }

  void setTTL(int ttl);
  void bind(const InetAddress &addr, bool reuseAddr);

  void connect(const InetAddress &peerAddr) 
  { connectModel_ = true; peerAddr_ = peerAddr; }

  void startRecv();
  void stopRecv();
  InetAddress getLocalAddr() const;

  const InetAddress &getPeerAddr() const 
  { assert(connectModel_); return peerAddr_; }

  // work only in connect model
  int send(const void* message, int len);
  int send(const StringPiece& message);
  int send(Buffer* message);

  // void send(const InetAddress& peerAddr, string&& message); // C++11
  int send(const InetAddress& addr, const void* message, int len);
  int send(const InetAddress& addr, const StringPiece& message);
  // void send(const InetAddress& peerAddr, Buffer&& message); // C++11
  int send(const InetAddress& addr, Buffer* message);  // this one will swap data

  // TODO(cbj): add multicast
  void setBroadcast(bool on);
  void setMulticastLoop(bool on);
  void setMulticastTTL(int ttl);
  void setMulticastInterface(const string& interfaceAddr);
  void setMemberShip(const string& multicastAddr, const string& interfaceAddr, bool join);

  /// Set message callback.
  /// Not thread safe.
  void setMessageCallback(const UdpMessageCallback& cb)
  { messageCallback_ = cb; }

  /// Set write complete callback.
  /// Not thread safe.
  void setWriteCompleteCallback(const UdpWriteCompleteCallback& cb)
  { writeCompleteCallback_ = cb; }

  /// Set high watermark callback.
  /// Not thread safe.
  void setHighWatermarkCallback(const UdpHighWaterMarkCallback& cb)
  { highWaterMarkCallback_ = cb; }

  void setStartedRecvCallback(const UdpStartedRecvCallback& cb)
  { startedRecvCallback_ = cb; }

  bool receiving() const { return receiving_; }

 private:
  typedef struct SendRequest
  {
    boost::weak_ptr<UdpSocket> socket;
    uv_udp_send_t req;
    Buffer buf;
    int messageId;
  } SendRequest;

  static void allocCallback(uv_handle_t *handle, size_t suggestedSize, uv_buf_t *buf);
  static void recvCallback(uv_udp_t *handle, 
                           ssize_t nread, 
                           const uv_buf_t *buf, 
                           const struct sockaddr *src, 
                           unsigned flag);

  static void sendCallback(uv_udp_send_t *req, int status);
  static void closeCallback(uv_handle_t *handle, int status);

  void sendInLoop(int messageId, const InetAddress &addr, const StringPiece& message);
  void sendInLoop(int messageId, const InetAddress &addr, const void* message, size_t len);

  inline SendRequest* getFreeSendReq();
  inline void releaseSendReq(SendRequest *req);
  void releaseAllSendReq();

  EventLoop* loop_;
  uv_udp_t *socket_;
  Buffer inputBuffer_;
  std::list<SendRequest*> freeSendReqList_;
  UdpMessageCallback messageCallback_;
  UdpWriteCompleteCallback writeCompleteCallback_;
  UdpStartedRecvCallback startedRecvCallback_;
  UdpHighWaterMarkCallback highWaterMarkCallback_;
  size_t bytesInSend_;
  size_t highWaterMark_;
  InetAddress peerAddr_;
  bool connectModel_;
  AtomicInt32 messageId_;
  bool receiving_;
};

typedef boost::shared_ptr<UdpSocket> UdpSocketPtr;

UdpSocket::SendRequest* UdpSocket::getFreeSendReq()
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

void UdpSocket::releaseSendReq(SendRequest *req)
{
  freeSendReqList_.push_back(req);
}

} // namespace net
} // namespace muduo

#endif // MUDUO_NET_UDPSOCKET_H
