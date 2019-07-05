#pragma once

#include "pollfd.h"
#include "reactor.h"

#include <functional>
#include <memory>

namespace infgen {

class epoll_backend : public backend {
private:
  file_desc epollfd_;
  void complete_epoll_event(poll_state &fd, int events, int event);
  void abort_fd();

public:
  epoll_backend();
  virtual void update(poll_state &fd, int event) override;
  virtual bool poll(int timeout) override;
  virtual void forget(poll_state &) override;
  virtual ~epoll_backend() override {}
};

} // namespace infgen
