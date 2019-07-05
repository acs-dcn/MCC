#pragma once

#include "pollfd.h"

namespace infgen {

class mtcp_epoll_backend : public backend {
private:
  mtcp_socket epollfd_;
  void complete_epoll_event(poll_state &fd, int events, int event);
  void abort_fd();

public:
  mtcp_epoll_backend();
  virtual void update(poll_state &fd, int event) override;
  virtual bool poll(int timeout) override;
  virtual void forget(poll_state&) override;
  virtual ~mtcp_epoll_backend() override {}
};


} // namespace infgen
