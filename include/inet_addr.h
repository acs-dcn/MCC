#pragma once
#include <netinet/ip.h>
#include <sys/socket.h>

#include "byteorder.h"

#include <fmt/ostream.h>

namespace infgen {

namespace net {
class inet_address;
}

class socket_address;
// ip and port are stored in host byte order
struct ipv4_addr {
  uint32_t ip;
  uint16_t port;

  ipv4_addr() : ip(0), port(0) {}
  ipv4_addr(uint32_t ip, uint16_t port) : ip(ip), port(port) {}
  ipv4_addr(uint16_t port) : ip(0), port(port) {}
  ipv4_addr(const std::string &addr)
      : ip(::inet_network(addr.c_str())), port(0) {}
  ipv4_addr(const std::string &addr, uint16_t port)
      : ip(::inet_network(addr.c_str())), port(port) {}
  ipv4_addr(const net::inet_address &, uint16_t);
  ipv4_addr(const socket_address &sa);
  ipv4_addr(socket_address &&sa);
};

// sockaddr stored in network byte order
class socket_address {
public:
  ::sockaddr_in in;
  bool any;
  union {
    ::sockaddr_in in;
    ::sockaddr sa;
    ::sockaddr_storage sas;
  } u;

  socket_address() {
    u.in.sin_family = AF_INET;
    u.in.sin_addr.s_addr = htonl(0);
    u.in.sin_port = htons(0);
    any = true;
  }

  socket_address(sockaddr_in sa) { 
    u.in = sa;
    any = false;
  }

  socket_address(ipv4_addr addr) {
    u.in.sin_family = AF_INET;
    u.in.sin_addr.s_addr = htonl(addr.ip);
    u.in.sin_port = htons(addr.port);
    any = false;
  }

};

inline
ipv4_addr::ipv4_addr(const socket_address& sa) {
  ip = ntoh(sa.u.in.sin_addr.s_addr);
  port = ntoh(sa.u.in.sin_port);
}

static inline 
std::ostream& operator<<(std::ostream& os, ipv4_addr addr ) {
  fmt::print(os, "{}.{}.{}.{}",
      (addr.ip >> 24) & 0xff,
      (addr.ip >> 16) & 0xff,
      (addr.ip >> 8) & 0xff,
      (addr.ip) & 0xff);
  return os << ":" << addr.port;
}

static inline
socket_address make_ipv4_address(ipv4_addr addr) {
  socket_address sa(addr);
  return sa;
}


} // namespace infgen
