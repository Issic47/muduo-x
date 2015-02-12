// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com), Bijin Chen

#include <muduo/net/TcpSocket.h>

#include <muduo/base/Logging.h>
#include <muduo/net/InetAddress.h>

#ifndef NATIVE_WIN32
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <strings.h>  // bzero
#include <stdio.h>  // snprintf
#endif

using namespace muduo;
using namespace muduo::net;


TcpSocket::TcpSocket( uv_tcp_t *socket ) 
  : socket_(CHECK_NOTNULL(socket))
{
}

TcpSocket::~TcpSocket()
{
}

uv_os_sock_t TcpSocket::fd() const
{
  uv_os_fd_t fd;
  int err = uv_fileno(
    reinterpret_cast<const uv_handle_t*>(socket_), &fd);
  if (err)
    LOG_ERROR << uv_strerror(err) << " in Socket::fd";
  return reinterpret_cast<uv_os_sock_t>(fd);
}


bool TcpSocket::getTcpInfo(struct tcp_info* tcpi) const
{
#ifdef TCP_INFO
  socklen_t len = sizeof(*tcpi);
  bzero(tcpi, len);
  return ::getsockopt(fd(), SOL_TCP, TCP_INFO, tcpi, &len) == 0;
#else
  return false;
#endif
}

bool TcpSocket::getTcpInfoString(char* buf, int len) const
{
#ifdef TCP_INFO
  struct tcp_info tcpi;
  bool ok = getTcpInfo(&tcpi);
  if (ok)
  {
    snprintf(buf, len, "unrecovered=%u "
      "rto=%u ato=%u snd_mss=%u rcv_mss=%u "
      "lost=%u retrans=%u rtt=%u rttvar=%u "
      "sshthresh=%u cwnd=%u total_retrans=%u",
      tcpi.tcpi_retransmits,  // Number of unrecovered [RTO] timeouts
      tcpi.tcpi_rto,          // Retransmit timeout in usec
      tcpi.tcpi_ato,          // Predicted tick of soft clock in usec
      tcpi.tcpi_snd_mss,
      tcpi.tcpi_rcv_mss,
      tcpi.tcpi_lost,         // Lost packets
      tcpi.tcpi_retrans,      // Retransmitted packets out
      tcpi.tcpi_rtt,          // Smoothed round trip time in usec
      tcpi.tcpi_rttvar,       // Medium deviation
      tcpi.tcpi_snd_ssthresh,
      tcpi.tcpi_snd_cwnd,
      tcpi.tcpi_total_retrans);  // Total retransmits for entire connection
  }
  return ok;
#else
  buf[0] = '\0';
  return false;
#endif
}

void TcpSocket::bindAddress(const InetAddress& localaddr, bool ipv6Only /*= false*/)
{
  int err = uv_tcp_bind(socket_, &localaddr.getSockAddr(), 
    ipv6Only ? UV_TCP_IPV6ONLY : 0);
  if (err) 
  {
    LOG_SYSFATAL << uv_strerror(err) << " in Socket::bindAddress";
  }
}

void TcpSocket::setSimultaneousAccept( bool on )
{
  int err = uv_tcp_simultaneous_accepts(socket_, on);
  if (err)
    LOG_SYSFATAL << uv_strerror(err) << " in Socket::setSimultaneousAccept";
}

void TcpSocket::listen(uv_connection_cb cb)
{
  int err = uv_listen(reinterpret_cast<uv_stream_t*>(socket_), 
                      SOMAXCONN, 
                      cb);
  if (err)
    LOG_SYSFATAL << uv_strerror(err) << " in Socket::listen";
}

int TcpSocket::accept( uv_tcp_t *client, InetAddress* peeraddr )
{
  int err = 0;
  do 
  {
    // NOTE: uv_accept handle EMFILE error use idle fd.
	err = uv_accept(reinterpret_cast<uv_stream_t*>(socket_), 
	                reinterpret_cast<uv_stream_t*>(client));
	if (err) break;
	
	sa addr;
	int nameLen = sizeof addr;
	err = uv_tcp_getpeername(client, &addr.u.sa, &nameLen);
	assert(err == 0);

    if (addr.u.sa.sa_family == AF_INET)
    {
	  peeraddr->setSockAddrInet(addr.u.in);
    }
    else // AF_INET6
    {
      assert(addr.u.sa.sa_family == AF_INET6);
      peeraddr->setSockAddrInet6(addr.u.in6);
    }

  } while (false);

  return err;
}

void TcpSocket::shutdownWrite(uv_shutdown_t *req, uv_shutdown_cb cb)
{
  int err = uv_shutdown(req, reinterpret_cast<uv_stream_t*>(socket_), cb);
  if (err) 
  {
    LOG_SYSFATAL << uv_strerror(err) << " in Socket::shutdownWrite";
  }
}

void TcpSocket::setTcpNoDelay(bool on)
{
  int err = uv_tcp_nodelay(socket_, on);
  if (err)
    LOG_SYSFATAL << uv_strerror(err) << " in Socket::setTcpNoDelay";
}

void TcpSocket::setReuseAddr(bool on)
{
  // In Unix-like system, socket is set SO_RESUSEADDR when binding.
  // In Windows, socket is not set SO_RESUSE_ADDR or SO_EXCLUSIVEADDREUSE when binding.
  // so this function doesn't work
#ifdef NATIVE_WIN32
  if (on)
    LOG_ERROR << "SO_REUSEADDR is not set in Windows";
#else
  if (!on)
    LOG_ERROR << "SO_REUSEADDR is set in Unix";
#endif
}

void TcpSocket::setReusePort(bool on)
{
#ifdef SO_REUSEPORT
  int optval = on ? 1 : 0;
  int ret = ::setsockopt(fd(), SOL_SOCKET, SO_REUSEPORT,
                         &optval, static_cast<socklen_t>(sizeof optval));
  if (ret < 0 && on)
  {
    LOG_SYSERR << "SO_REUSEPORT failed";
  }
#else
  if (on)
  {
    LOG_ERROR << "SO_REUSEPORT is not supported";
  }
#endif
}

void TcpSocket::setKeepAlive(bool on)
{
  // WARNING: In libuv 1.0.1, delay is set to 60 when binding.
  int err = uv_tcp_keepalive(socket_, on, 60);
  if (err)
    LOG_SYSFATAL << uv_strerror(err) << " in Socket::setKeepAlive";
}

bool TcpSocket::isSelfConnect(uv_tcp_t *socket)
{
  int err = 0;
  bool selfConnect = false;

  do
  {
    struct sa localAddr = TcpSocket::getLocalAddr(socket);
    struct sa peerAddr = TcpSocket::getPeerAddr(socket);
    if (localAddr.u.sa.sa_family != peerAddr.u.sa.sa_family)
      break;

    if (localAddr.u.sa.sa_family == AF_INET)
    {
      selfConnect = localAddr.u.in.sin_port == peerAddr.u.in.sin_port &&
        localAddr.u.in.sin_addr.s_addr == peerAddr.u.in.sin_addr.s_addr;
    }
    else // AF_INET6
    {
      selfConnect = localAddr.u.in6.sin6_port == peerAddr.u.in6.sin6_port &&
        0 == memcmp(&localAddr.u.in6.sin6_addr, &peerAddr.u.in6.sin6_addr, sizeof(in6_addr));
    }

  } while (false);

  if (err)
  {
    LOG_ERROR << uv_strerror(err) << " in Socket::isSelfConnect";
    return false;
  }
  return selfConnect;
}


struct sa TcpSocket::getLocalAddr( uv_tcp_t *socket )
{
  struct sa localAddr;
  int localLen = sizeof localAddr;
  int err = uv_tcp_getsockname(socket, &localAddr.u.sa, &localLen);
  if (err)
  {
    LOG_SYSERR << uv_strerror(err) << " in Socket::getLocalAddr";
  }
  return localAddr;
}

struct sa TcpSocket::getPeerAddr( uv_tcp_t *socket )
{
  struct sa peerAddr;
  int localLen = sizeof peerAddr;
  int err = uv_tcp_getpeername(socket, &peerAddr.u.sa, &localLen);
  if (err)
  {
    LOG_SYSERR << uv_strerror(err) << " in Socket::getPeerAddr";
  }
  return peerAddr;
}
