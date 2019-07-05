#pragma once

#include <cassert>
#include <cerrno>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>

#include <iostream>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace infgen {

inline void throw_system_error_on(bool cond, const char *what_arg) {
  if (cond) {
    throw std::system_error(errno, std::system_category(), what_arg);
  }
}

template <typename T>
inline void throw_kernel_error(T r) {
  static_assert(std::is_signed<T>::value,
                "kernel error variable must be signed");
  if (r < 0) {
    throw std::system_error(-r, std::system_category());
  }
}

class file_desc {
 private:
  int fd_;

 public:
  file_desc() { fd_ = -1; }
  file_desc(int x) : fd_(x) {}
  file_desc(const file_desc &x) : fd_(x.fd_) {}
  file_desc(file_desc &&x) : fd_(x.fd_) { x.fd_ = -1; }
  void operator=(const file_desc &) = delete;
  file_desc &operator=(file_desc &&x) {
    if (this != &x) {
      std::swap(fd_, x.fd_);
      if (x.fd_ != -1) {
        x.close();
      }
    }
    return *this;
  }

  int get() const { return fd_; }

  void close() {
    assert(fd_ != -1);
    auto ret = ::close(fd_);
    throw_system_error_on(ret == -1, "close");
    fd_ = -1;
  }

  static file_desc open(std::string name, int flags, mode_t mode = 0) {
    int fd = ::open(name.c_str(), flags, mode);
    throw_system_error_on(fd == -1, "open");
    return file_desc(fd);
  }

  static file_desc socket(int domain, int type, int protocol) {
    int fd = ::socket(domain, type, protocol);
    throw_system_error_on(fd == -1, "socket");
    return file_desc(fd);
  }

  static file_desc eventfd(unsigned int initval, int flags) {
    int fd = ::eventfd(initval, flags);
    throw_system_error_on(fd == -1, "eventfd");
    return file_desc(fd);
  }

  static file_desc epoll_create(int flags) {
    int fd = ::epoll_create1(flags);
    throw_system_error_on(fd == -1, "epoll create");
    return file_desc(fd);
  }

  file_desc accept(struct sockaddr addr, socklen_t *addrlen, int flags = 0) {
    int fd = ::accept4(fd_, &addr, addrlen, flags);
    throw_system_error_on(fd == -1, "accept");
    int flag = fcntl(fd, F_GETFL);
    ::fcntl(fd, F_SETFL, flag | O_NONBLOCK);
    throw_system_error_on(fd == -1, "fcntl");
    return file_desc(fd);
  }

  void bind(const struct sockaddr addr, socklen_t addrlen) {
    auto r = ::bind(fd_, &addr, addrlen);
    throw_system_error_on(r == -1, "bind");
  }

  void listen(int backlog) {
    auto r = ::listen(fd_, backlog);
    throw_system_error_on(r == -1, "listen");
  }

  void connect(const struct sockaddr addr, socklen_t addrlen) {
    auto r = ::connect(fd_, &addr, addrlen);
    if (r == -1 && errno == EINPROGRESS) {
      return;
    }
    throw_system_error_on(r == -1, "connect");
  }

  template <typename Data>
  Data getsockopt(int level, int optname) {
    Data data;
    socklen_t len = sizeof(data);
    auto r = ::getsockopt(fd_, level, optname, &data, &len);
    throw_system_error_on(r == -1, "getsockopt");
    return data;
  }

  void getsockname(struct sockaddr *addr) {
    socklen_t len = sizeof(addr);
    auto r = ::getsockname(fd_, addr, &len);
    throw_system_error_on(r == -1, "getsockname");
  }

  void getpeername(struct sockaddr *peer) {
    socklen_t len = sizeof(peer);
    auto r = ::getpeername(fd_, peer, &len);
    throw_system_error_on(r == -1, "getpeername");
  }

  size_t write(const void *buf, size_t count) {
    auto r = ::write(fd_, buf, count);
    if (r == -1 && errno == EAGAIN) {
      return 0;
    }
    throw_system_error_on(r == -1, "write");
    return r;
  }

  std::optional<size_t> read(void *buf, size_t count) {
    auto r = ::read(fd_, buf, count);
    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return {};
    }
    throw_system_error_on(r == -1, "read");
    return {size_t(r)};
  }

  std::optional<size_t> send(const void *buf, size_t count, int flags) {
    auto r = ::send(fd_, buf, count, flags);
    if (r == -1 && errno == EAGAIN) {
      return {};
    }
    throw_system_error_on(r == -1, "send");
    return {size_t(r)};
  }

  void shutdown(int how) {
    auto r = ::shutdown(fd_, how);
    if (r == -1 && errno != ENOTCONN) {
      throw_system_error_on(r == -1, "shutdown");
    }
  }
};

struct posix_socket {
  static void set_no_delay(int fd, bool enable) {
    int nodelay = enable ? 1 : 0;
    auto r = ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay,
                          sizeof(int));
    throw_system_error_on(r == -1, "setnodelay");
  }

  static void set_reuseport(int sockfd) {
    int optval = 1;
    auto r = ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    throw_system_error_on(r == -1, "setreuseport");
  }

  /*
  static void set_zero_copy(int fd, bool enable) {
    int zero_copy = enable ? 1: 0;
    ::setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, (const char*)&zero_copy,
  sizeof(int));
  }
  */

  static void set_reuseaddr(int sockfd) {
    int reuse = 1;
    auto r = ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                        (const char *)&reuse, sizeof(reuse));
    throw_system_error_on(r == -1, "setreuseaddr");
  }

  static void getifaddrs(struct ifaddrs *addr) {
    auto r = ::getifaddrs(&addr);
    throw_system_error_on(r == -1, "getifaddr");
  }

  static std::string getnameinfo(const struct sockaddr *addr) {
    char host[NI_MAXHOST];
    auto r = ::getnameinfo(addr, sizeof(sockaddr_in), host, NI_MAXHOST, nullptr,
                           0, NI_NUMERICHOST);
    throw_system_error_on(r == -1, "getnameinfo");
    return std::string(host);
  }
};

}  // namespace infgen
