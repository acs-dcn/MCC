#include "log.h"
#include "pollfd.h"
#include "reactor.h"

namespace infgen {

extern logger net_logger;

poll_state::~poll_state() {}

void poll_state::replace(int id) {
  pollid = id;
  events_requested = 0;
  events_epoll = 0;
}

pollable_fd::~pollable_fd() {

}

void pollable_fd::attach_to_loop() {
  engine().update(*state_, EPOLLIN | EPOLLOUT);
}

void pollable_fd::detach_from_loop() {
  engine().forget(*state_);
}

void pollable_fd::update_state(int events) { engine().update(*state_, events); }

} // namespace infgen
