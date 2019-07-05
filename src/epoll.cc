#include "log.h"
#include "reactor.h"
#include "epoll.h"

namespace infgen {

using namespace net;

extern logger epoll_logger;

epoll_backend::epoll_backend()
    : epollfd_(file_desc::epoll_create(EPOLL_CLOEXEC)) {}

bool epoll_backend::poll(int timeout) {
  std::array<epoll_event, 128> events;
  int nr = ::epoll_wait(epollfd_.get(), events.data(), events.size(), timeout);
  if (nr == -1 && errno == EINTR) {
    return false;
  }
  assert(nr != -1);
  for (int i = 0; i < nr; ++i) {
    auto &ev = events[i];
    auto state = reinterpret_cast<poll_state *>(ev.data.ptr);
    auto events = ev.events & (EPOLLIN | EPOLLOUT);
    auto events_to_remove = events & ~state->events_requested;

    epoll_logger.debug("events: {}, requested: {}, remove: {}", events,
                     state->events_requested, events_to_remove);

    complete_epoll_event(*state, events, EPOLLOUT);
    complete_epoll_event(*state, events, EPOLLIN);

    if (events_to_remove) {
      epoll_logger.trace("remove events {} for fd {}", events_to_remove,
                       state->pollid);
      state->events_epoll &= ~events_to_remove;
      ev.events = state->events_epoll;
      auto op = ev.events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
      ::epoll_ctl(epollfd_.get(), op, state->pollid, &ev);
    }
  }
  return nr;
}

void epoll_backend::update(poll_state &state, int event) {
  state.events_requested |= event;
  if (!(state.events_epoll & event)) {
    epoll_logger.trace("update event {} for fd {}", event, state.pollid);
    auto ctl = state.events_epoll ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    state.events_epoll |= event;
    ::epoll_event ev;
    ev.events = state.events_epoll;
    ev.data.ptr = &state;
    int r = ::epoll_ctl(epollfd_.get(), ctl, state.pollid, &ev);
    epoll_logger.fatalif(r != 0, "epoll_ctl failed, {}: {}", errno, strerror(errno));
    engine().start_epoll();
  }
}

void epoll_backend::complete_epoll_event(poll_state &state, int events, int event) {
  if (state.events_requested & events & event) {
    state.events_requested &= ~event;
    if (event & EPOLLOUT) {
      epoll_logger.trace("Fd {} EPOLLOUT triggered!", state.pollid);
      state.pollout();
    }

    if (event & EPOLLIN) {
      epoll_logger.trace("Fd {} EPOLLIN triggered!", state.pollid);
      state.pollin();
    }
  }
}

void epoll_backend::forget(poll_state &state) {
  if (state.events_epoll) {
    ::epoll_ctl(epollfd_.get(), EPOLL_CTL_DEL, state.pollid, nullptr);
  }
}
} // namespace infgen
