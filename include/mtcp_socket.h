#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <netinet/in.h>

#include "mtcp_api.h"
#include "mtcp_epoll.h"

#include "reactor.h"

namespace infgen {

inline void throw_mtcp_error_on(bool cond, const char *what_arg) {
  if (cond) {
    throw std::system_error(errno, std::system_category(), what_arg);
  }
}

struct mtcp_socket {
  int id;
  mctx_t mctx;

  mtcp_socket(int fd, mctx_t ctx) : id(fd), mctx(ctx) {}
  mtcp_socket() { id = -1; }

  mtcp_socket(const mtcp_socket &x) : id(x.id), mctx(x.mctx) {}
  mtcp_socket(mtcp_socket &&x) : id(x.id), mctx(x.mctx) {
    x.id = -1;
    x.mctx = nullptr;
  }
  void operator=(const mtcp_socket &) = delete;
  mtcp_socket &operator=(mtcp_socket &&x) {
    if (this != &x) {
      std::swap(id, x.id);
      mctx = x.mctx;
      if (x.id == -1) {
        x.mctx = nullptr;
        x.close();
      }
    }
    return *this;
  }

  int get() const { return id; }
  mctx_t ctx() const { return mctx; }

  void close() {
    if (id == -1) return;
    auto ret = mtcp_close(mctx, id);
    throw_mtcp_error_on(ret == -1, "mtcp close");
    id = -1;
  }

  static mtcp_socket socket(int domain, int type, int protocol) {
    int id = ::mtcp_socket(engine().context(), domain, type, protocol);
    throw_mtcp_error_on(id == -1, "mtcp socket");
    return mtcp_socket(id, engine().context());
  }

  static mtcp_socket epoll_create(int flags) {
    int id = mtcp_epoll_create1(engine().context(), flags);
    throw_mtcp_error_on(id == -1, "mtcp epoll create");
    return mtcp_socket(id, engine().context());
  }

  /*
  static mtcp_socket accept(struct sockaddr addr, socklen_t *addrlen, int flags = 0) {
    mctx_t mctx = mtcp_create_context(engine().cpu_id());
    int id = mtcp_accept(mctx, id, &addr, addrlen, flags);
    throw_mtcp_error_on(id == -1, "mtcp accept");
    return mtcp_socket(id, mctx);
  }
  */

  void bind(const struct sockaddr addr, socklen_t addrlen) {
    auto r = mtcp_bind(mctx, id, &addr, addrlen);
    throw_mtcp_error_on(r == -1, "mtcp bind");
  }

  void listen(int backlog) {
    auto r = mtcp_listen(mctx, id, backlog);
    throw_mtcp_error_on(r == -1, "listen");
  }


  void connect(const struct sockaddr addr, socklen_t addrlen) {
    auto r = mtcp_connect(mctx, id, &addr, addrlen);
    if (r == -1 && errno == EINPROGRESS) {
      return;
    }
    throw_mtcp_error_on(r == -1, "mtcp connect");
  }

  template <typename Data> Data getsockopt(int level, int optname) {
    Data data;
    socklen_t len = sizeof(data);
    auto r = mtcp_getsockopt(mctx, id, level, optname, &data, &len);
    if (r == -1 && errno == EINPROGRESS) {
      return -1;
    }
    throw_mtcp_error_on(r == -1, "mtcp getsockopt");
    return data;
  }

  size_t write(const char *buf, size_t count) {
    auto r = mtcp_write(mctx, id, buf, count);
    if (r == -1 && errno == EAGAIN) {
      return 0;
    }
    throw_mtcp_error_on(r == -1, "mtcp write");
    return r;
  }

  std::optional<size_t> read(char *buf, size_t count) {
    auto r = mtcp_recv(mctx, id, buf, count, 0);
    if (r == -1 && errno == EAGAIN) {
      return {};
    }
    throw_mtcp_error_on(r == -1, "mtcp read");
    return {size_t(r)};
  }

  void getsockname(int sockfd, struct sockaddr* addr) {
    socklen_t solen = sizeof(addr);
    auto r = mtcp_getsockname(mctx, sockfd, addr, &solen);
    throw_mtcp_error_on(r == -1, "mtcp getsockname");
  }

  void set_nonblock() {
    auto r = mtcp_setsock_nonblock(mctx, id);
    throw_mtcp_error_on(r == -1, "mtcp set nonblock");
  }

  // epoll related API
  void epoll_ctl(int op, int sockid, struct mtcp_epoll_event *event) {
    int r = mtcp_epoll_ctl(mctx, id, op, sockid, event);
    throw_mtcp_error_on(r == -1, "mtcp_epoll_ctl");
  }

  int epoll_wait(struct mtcp_epoll_event* events, int maxevents, int timeout) {
    int nr = mtcp_epoll_wait(mctx, id, events, maxevents, timeout);
    throw_mtcp_error_on(nr == -1, "mtcp epoll wait");
    return nr;
  }


};



} // namespace infgen
