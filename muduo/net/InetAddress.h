// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_INETADDRESS_H
#define MUDUO_NET_INETADDRESS_H

#include <muduo/base/copyable.h>
#include <muduo/base/StringPiece.h>

#ifndef NATIVE_WIN32
#include <netinet/in.h>
#endif // !NATIVE_WIN32


namespace muduo
{
namespace net
{

/** Defines a Socket Address */
struct sa {
  union {
    struct sockaddr sa;
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
    uint8_t padding[28];
  } u;
  socklen_t len;
};

///
/// Wrapper of sockaddr_in.
///
/// This is an POD interface class.
class InetAddress : public muduo::copyable
{
 public:
  /// Constructs an endpoint with given port number.
  /// Mostly used in TcpServer listening.
  explicit InetAddress(int af = AF_INET, uint16_t port = 0, bool loopbackOnly = false);

  /// Constructs an endpoint with given ip and port.
  /// @c ip should be "1.2.3.4"
  InetAddress(int af, StringArg ip, uint16_t port);

  /// Constructs an endpoint with given struct @c sa
  /// Mostly used when accepting new connections
  InetAddress(const struct sa& addr);

  /// Constructs an endpoint with given struct @c sockaddr
  /// Mostly used when receiving data in UDP
  InetAddress(const struct sockaddr& addr);

  /// Constructs an endpoint with given struct @c sockaddr_in
  /// Mostly used when accepting new connections
  InetAddress(const struct sockaddr_in& addr) 
  { 
    addr_.u.in = addr; 
    addr_.len = sizeof(addr);
  }

  /// Constructs an endpoint with given struct @c sockaddr_in6
  /// Mostly used when accepting new connections
  InetAddress(const struct sockaddr_in6& addr) 
  { 
    addr_.u.in6 = addr; 
    addr_.len = sizeof(addr);
  }

  string toIp() const;
  string toIpPort() const;
  uint16_t toPort() const;

  // default copy/assignment are Okay
  const struct sockaddr& getSockAddr() const { return addr_.u.sa; }

  const struct sockaddr_in& getSockAddrInet() const { return addr_.u.in; }
  void setSockAddrInet(const struct sockaddr_in& addr) 
  { 
    addr_.u.in = addr; 
    addr_.len = sizeof(addr);
  }

  const struct sockaddr_in6& getSockAddrInet6() const { return addr_.u.in6; }
  void setSockAddrInet6(const struct sockaddr_in6& addr) 
  {
    addr_.u.in6 = addr; 
    addr_.len = sizeof(addr);
  }

  uint32_t ip4NetEndian() const { return addr_.u.in.sin_addr.s_addr; }
  struct in6_addr ip6NetEndian() const { return addr_.u.in6.sin6_addr; }
  uint16_t portNetEndian() const { return addr_.u.in.sin_port; }

  // resolve hostname to IP address, not changing port or sin_family
  // return true on success.
  // thread safe
  static bool resolve(StringArg hostname, InetAddress* result);
  // static std::vector<InetAddress> resolveAll(const char* hostname, uint16_t port = 0);

 private:
   struct sa addr_;
};

}
}

#endif  // MUDUO_NET_INETADDRESS_H
