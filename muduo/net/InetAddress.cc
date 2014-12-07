// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/InetAddress.h>

#include <muduo/base/Logging.h>
#include <muduo/net/Endian.h>
#include <muduo/net/SocketsOps.h>

#ifndef NATIVE_WIN32
#include <netdb.h>
#include <strings.h>  // bzero
#include <netinet/in.h>
#endif // !NATIVE_WIN32

#include <boost/static_assert.hpp>

//     /* Structure describing an Internet socket address.  */
//     struct sockaddr_in {
//         sa_family_t    sin_family; /* address family: AF_INET */
//         uint16_t       sin_port;   /* port in network byte order */
//         struct in_addr sin_addr;   /* internet address */
//     };

//     /* Internet address. */
//     typedef uint32_t in_addr_t;
//     struct in_addr {
//         in_addr_t       s_addr;     /* address in network byte order */
//     };

using namespace muduo;
using namespace muduo::net;

muduo::net::InetAddress::InetAddress( int af /*= AF_NET*/, uint16_t port /*= 0*/, bool loopbackOnly /*= false*/ )
{
  bzero(&addr_, sizeof addr_);
  switch (af)
  {
  case AF_INET:
    addr_.u.in.sin_family = af;
    addr_.u.in.sin_port = htons(port);
    addr_.u.in.sin_addr.s_addr = 
      loopbackOnly ? htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);
    addr_.len = sizeof(addr_.u.in);
    break;

  case AF_INET6:
    addr_.u.in6.sin6_family = af;
    addr_.u.in6.sin6_port = htons(port);
    if (loopbackOnly) 
    {
      IN6_SET_ADDR_LOOPBACK(&addr_.u.in6.sin6_addr);
    }
    else
    {
      IN6_SET_ADDR_UNSPECIFIED(&addr_.u.in6.sin6_addr);
    }
    addr_.len = sizeof(addr_.u.in6);
    break;

  default:
    LOG_SYSERR << "No support address family:" << af;
    break;
  }

}

InetAddress::InetAddress(int af, StringArg ip, uint16_t port)
{
  bzero(&addr_, sizeof addr_);
  int err = 0;
  switch (af)
  {
  case AF_INET:
    err = uv_ip4_addr(ip.c_str(), port, &addr_.u.in);
    addr_.len = sizeof(addr_.u.in);
    break;

  case AF_INET6:
    err = uv_ip6_addr(ip.c_str(), port, &addr_.u.in6);
    addr_.len = sizeof(addr_.u.in6);
    break;

  default:
    err = UV_EAI_ADDRFAMILY;
    break;
  }
  if (err)
    LOG_SYSERR << uv_strerror(err) 
              << "(" << af << "," << ip.c_str() << ":" << port << ")";
}

muduo::net::InetAddress::InetAddress( const struct sockaddr& addr )
{
  bzero(&addr_, sizeof addr_);
  addr_.u.sa = addr; 
  switch (addr.sa_family)
  {
  case AF_INET:
    addr_.len = sizeof(addr_.u.in);
    break;

  case AF_INET6:
    addr_.len = sizeof(addr_.u.in6);
    break;

  default:
    LOG_SYSERR << "No support address family:" << addr.sa_family;
    break;
  }
}

string InetAddress::toIpPort() const
{
  char buf[INET6_ADDRSTRLEN];
  int err = 0;
  switch (addr_.u.sa.sa_family)
  {
  case AF_INET:
    err = uv_ip4_name(&addr_.u.in, buf, sizeof buf);
    break;

  case AF_INET6:
    err = uv_ip6_name(&addr_.u.in6, buf, sizeof buf);
    break;

  default:
    err = UV_EAI_ADDRFAMILY;
    break;
  }
  if (err) 
  {
    buf[0] = '\0';
    LOG_SYSERR << uv_strerror(err);
  }

  return buf;
}

string InetAddress::toIp() const
{
  char buf[INET6_ADDRSTRLEN];
  int err = 0;
  switch (addr_.u.sa.sa_family)
  {
  case AF_INET:
    err = uv_inet_ntop(AF_INET, &addr_.u.in.sin_addr, buf, sizeof buf);
    break;
  case AF_INET6:
    err = uv_inet_ntop(AF_INET6, &addr_.u.in6.sin6_addr, buf, sizeof buf);
  default:
    err = UV_EAI_ADDRFAMILY;
    break;
  }

  if (err)
  {
    buf[0] = '\0';
    LOG_SYSERR << uv_strerror(err);
  }
  
  return buf;
}

uint16_t InetAddress::toPort() const
{
  // NOTE: the offset of sin_port in sockaddr_in equals in sockadd_in6
  return ntohs(addr_.u.in.sin_port);
}

//static thread_local char t_resolveBuffer[64 * 1024];

bool InetAddress::resolve(StringArg hostname, InetAddress* out)
{
  //assert(out != NULL);
  //struct hostent hent;
  //struct hostent* he = NULL;
  //int herrno = 0;
  //bzero(&hent, sizeof(hent));

  //int ret = gethostbyname_r(hostname.c_str(), &hent, t_resolveBuffer, sizeof t_resolveBuffer, &he, &herrno);
  //if (ret == 0 && he != NULL)
  //{
  //  assert(he->h_addrtype == AF_INET && he->h_length == sizeof(uint32_t));
  //  out->addr_.sin_addr = *reinterpret_cast<struct in_addr*>(he->h_addr);
  //  return true;
  //}
  //else
  //{
  //  if (ret)
  //  {
  //    LOG_SYSERR << "InetAddress::resolve";
  //  }
  //  return false;
  //}
  return false;
}
