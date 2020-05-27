#include "log.h"
#include "mepoll.h"

namespace infgen {

using namespace net;

logger epoll_logger("epoll");

mtcp_epoll_backend::mtcp_epoll_backend()
    : epollfd_(mtcp_socket::epoll_create(EPOLL_CLOEXEC)) {
  epoll_logger.info("mtcp epollfd {} created", epollfd_.get());
}

bool mtcp_epoll_backend::poll(int timeout = 0) {
  std::array<mtcp_epoll_event, 128> events;
  int nr = epollfd_.epoll_wait(events.data(), events.size(), 0);

  if (nr == -1 && errno == EINTR) {
    return false;
  }

  for (int i = 0; i < nr; ++i) {
    auto &ev = events[i];
    auto state = reinterpret_cast<poll_state *>(ev.data.ptr);
    auto events = ev.events & (MTCP_EPOLLIN | MTCP_EPOLLOUT);
    auto events_to_remove = events & ~state->events_requested;

    epoll_logger.debug("events: {}, requested: {}, remove: {}", events,
                     state->events_requested, events_to_remove);

    complete_epoll_event(*state, events, MTCP_EPOLLIN);
    complete_epoll_event(*state, events, MTCP_EPOLLOUT);
#if 0
    if (events_to_remove) {
      epoll_logger.trace("remove events {} for socket {}", events_to_remove,
                       state->pollid);
      state->events_epoll &= ~events_to_remove;
      ev.events = state->events_epoll;
      auto op = ev.events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
      try {
        epollfd_.epoll_ctl(op, state->pollid, &ev);
      } catch (std::system_error& e) {
				if (op == EPOLL_CTL_MOD) {
					epoll_logger.error("socket {}, EPOLL_CTL_MOD, {}", state->pollid, e.what());
				} else {
					epoll_logger.error("socket {}, EPOLL_CTL_DEL, {}", state->pollid, e.what());
				}
        //engine().stop();
      }
    }
#endif
  }
  return nr;
}

void mtcp_epoll_backend::update(poll_state &state, int event) {
  state.events_requested |= event;
  if (!(state.events_epoll & event)) {
    epoll_logger.trace("update event {} for socket {}", event, state.pollid);
    auto op = state.events_epoll ? MTCP_EPOLL_CTL_MOD : MTCP_EPOLL_CTL_ADD;
    state.events_epoll |= event;
    mtcp_epoll_event ev;
    ev.events = state.events_epoll;
    ev.data.ptr = &state;

    try {
      epollfd_.epoll_ctl(op, state.pollid, &ev);
    } catch (std::system_error& e) {
      epoll_logger.error("update fd {} state error, {}", e.what());
      engine().stop();
    }


    engine().start_mtcp_epoll();
  }
}

void mtcp_epoll_backend::complete_epoll_event(poll_state &state, int events, int event) {
  if (state.events_requested & events & event) {
    state.events_requested &= ~event;
    if (event & MTCP_EPOLLOUT) {
      epoll_logger.trace("Socket {} EPOLLOUT triggered!", state.pollid);
      state.pollout();
    }

    if (event & MTCP_EPOLLIN) {
      epoll_logger.trace("Socket {} EPOLLIN triggered!", state.pollid);
      state.pollin();
    }
  }
}

void mtcp_epoll_backend::forget(poll_state &state) {
  if (state.events_epoll) {
    try {
      epoll_logger.trace("removing polled socket {}", state.pollid);
      epollfd_.epoll_ctl(MTCP_EPOLL_CTL_DEL, state.pollid, nullptr);
    } catch (std::system_error& e) {
      epoll_logger.error("remove polled socket {} error, {}", state.pollid, e.what());
      engine().stop();
    }
  }
}
} // namespace infgen
