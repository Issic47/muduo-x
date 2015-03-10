// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_SOCKET_H
#define MUDUO_NET_SOCKET_H

#include <boost/noncopyable.hpp>
#include <uv.h>

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace muduo
{
///
/// TCP networking.
///
namespace net
{

class InetAddress;
class EventLoop;

///
/// Wrapper of socket file descriptor.
///
/// It closes the sockfd when desctructs.
/// It's thread safe, all operations are delagated to OS.
class TcpSocket : boost::noncopyable
{
 public:
  // Socket instance will own this socket
  explicit TcpSocket(uv_tcp_t *socketHandle);

  // Socket(Socket &&other);

  ~TcpSocket();

  static bool isSelfConnect(uv_tcp_t *socket);
  static struct sa getLocalAddr(uv_tcp_t *socket);
  static struct sa getPeerAddr(uv_tcp_t *socket);

  uv_os_sock_t fd() const;
  uv_tcp_t* socket() const { return socket_; }

  // return true if success.
  bool getTcpInfo(struct tcp_info*) const;
  bool getTcpInfoString(char* buf, int len) const;

  /// abort if address in use
  void bindAddress(const InetAddress& localaddr, bool ipv6Only = false);

  /// abort if address in use
  void listen(uv_connection_cb cb);

  /// On success, returns a non-negative integer that is
  /// a descriptor for the accepted socket, which has been
  /// set to non-blocking and close-on-exec. *peeraddr is assigned.
  /// On error, -1 is returned, and *peeraddr is untouched.
  /// WARNING: client must be initialized before this function called
  int accept(uv_tcp_t *client, InetAddress* peeraddr);

  void shutdownWrite(uv_shutdown_t *req, uv_shutdown_cb cb);

  void setSimultaneousAccept(bool on);

  ///
  /// Enable/disable TCP_NODELAY (disable/enable Nagle's algorithm).
  ///
  void setTcpNoDelay(bool on);

  ///
  /// Enable/disable SO_REUSEADDR
  ///
  void setReuseAddr(bool on);

  ///
  /// Enable/disable SO_REUSEPORT
  ///
  void setReusePort(bool on);

  ///
  /// Enable/disable SO_KEEPALIVE
  ///
  void setKeepAlive(bool on);


  void setData(void *data) { socket_->data = data; }

  void* getData() { return socket_->data; }

  uv_loop_t* GetUVLoop() const { return socket_->loop; }

  size_t getWriteQueueSize() const { return socket_->write_queue_size; }

  int readStart(uv_alloc_cb allocCallback, uv_read_cb readCallback)
  {
    return uv_read_start(reinterpret_cast<uv_stream_t*>(socket_),
                         allocCallback, 
                         readCallback);
  }

  int readStop()
  {
    return uv_read_stop(reinterpret_cast<uv_stream_t*>(socket_));
  }

  int write(uv_write_t *req, const uv_buf_t bufs[], unsigned int nbufs, uv_write_cb writeCallback)
  {
    return uv_write(req, reinterpret_cast<uv_stream_t*>(socket_), bufs, nbufs, writeCallback);
  }

  int tryWrite(const uv_buf_t bufs[], unsigned int nbufs) 
  {
    return uv_try_write(reinterpret_cast<uv_stream_t*>(socket_), bufs, nbufs);
  }
  
 private:
  uv_tcp_t *socket_;
};

}
}
#endif  // MUDUO_NET_SOCKET_H
