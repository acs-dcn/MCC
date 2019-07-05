#pragma once

#include <pthread.h>
#include <signal.h>

#include <system_error>

namespace infgen {

template <typename T>
inline void throw_pthread_error(T r) {
  if (r != 0) {
    throw std::system_error(r, std::system_category());
  }
}

inline
sigset_t make_sigset_mask(int signo) {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, signo);
  return set;
}

inline
sigset_t make_full_sigset_mask() {
  sigset_t set;
  sigfillset(&set);
  return set;
}

inline
sigset_t make_empty_sigset_mask() {
  sigset_t set;
  sigemptyset(&set);
  return set;
}

inline void pin_this_thread(unsigned cpu_id) {
  cpu_set_t cs;
  CPU_ZERO(&cs);
  CPU_SET(cpu_id, &cs);
  auto r = pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
  if (r != 0) {
    exit(-1);
  }
}

} // namespace infgen
