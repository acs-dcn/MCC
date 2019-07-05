#pragma once

#include "posix.h"
#include "mtcp_socket.h"

#include <functional>
#include <memory>

namespace infgen {

struct poll_state {
  using eventfunc = std::function<void()>;

  poll_state(int id) : pollid(id) {}
  ~poll_state();
  poll_state(const poll_state &) = delete;
  void operator=(const poll_state &) = delete;
  void replace(int id);

  int pollid;

  int events_epoll = 0;
  int events_requested = 0;

  eventfunc pollin;
  eventfunc pollout;
};

class pollable_fd {
public:
  pollable_fd(file_desc fd)
      : state_(std::make_unique<poll_state>(fd.get())), fd_(std::move(fd)) {}
  pollable_fd(mtcp_socket socket)
      : state_(std::make_unique<poll_state>(socket.get())),
        socket_(std::move(socket)) {}

  ~pollable_fd();

  pollable_fd(pollable_fd &&) = default;
  pollable_fd &operator=(pollable_fd &&) = default;

  void attach_to_loop();
  void detach_from_loop();

  template <typename Func> void when_writable(Func &&func) {
    state_->pollout = std::forward<Func>(func);
  }

  template <typename Func> void when_readable(Func &&func) {
    state_->pollin = std::forward<Func>(func);
  }

  void enable_read() { update_state(EPOLLIN); }
  void enable_write() { update_state(EPOLLOUT); }

  file_desc &get_file_desc() { return fd_; }
  mtcp_socket &get_mtcp_socket() { return socket_; }

  void shutdown(int how) { fd_.shutdown(how); }
  void close_socket() { socket_.close(); }
  void close_fd() { fd_.close(); }
  int get_id() const { return state_->pollid; }

private:
  void update_state(int events);
  std::unique_ptr<poll_state> state_;
  file_desc fd_;
  mtcp_socket socket_;
};
} // namespace infgen
