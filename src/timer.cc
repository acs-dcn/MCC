#include "timer.h"
#include "log.h"

namespace infgen {
extern logger net_logger;

namespace internal {

uint64_t timer_manager::g_timer_id = 0;

size_t timer_manager::size() {
  return timers_.size();
}

void timer_manager::tick() {
  if (timers_.empty()) {
    return;
  }

  for (auto it = timers_.begin(); it != timers_.end(); it++) {
    auto now = system_clock::now();
    if (now < it->first) {
      return;
    }
    it->second.alarm();
    timer t(std::move(it->second));
    timers_.erase(it);

    if (t.count_ != 0) {
      const auto tp = t.id_->first;
      auto it_new = timers_.insert(std::make_pair(tp, std::move(t)));
      if (it == timers_.end() || it_new->first < it->first) {
        it = it_new;
      }
    }

  }
}

bool timer_manager::cancel(timer_id id) {
  return true;
}

microseconds timer_manager::latest_timeout() const {
  if (timers_.empty()) {
    return microseconds::max();
  }
  const auto &timer = timers_.begin()->second;
  auto now = system_clock::now();
  if (now > timers_.begin()->first) {
    return microseconds::min();
  } else {
    return std::chrono::duration_cast<microseconds>(timer.id()->first - now);
  }
}
timer_manager::timer::timer(const system_clock::time_point& tp):
  id_(new std::pair<system_clock::time_point, uint64_t> { tp, ++timer_manager::g_timer_id }),
  count_(infinite) {}

timer_manager::timer::timer(timer &&t)
    : func_(std::move(t.func_)), id_(std::move(t.id_)),
      interval_(std::move(t.interval_)), count_(t.count_) {}

timer_manager::timer &timer_manager::timer::operator=(timer &&t) {
  if (this != &t) {
    func_ = std::move(t.func_);
    id_ = std::move(t.id_);
    interval_ = std::move(t.interval_);
    count_ = t.count_;
  }
  return *this;
}

void timer_manager::timer::alarm() {
  if (!func_ || count_ == 0) {
    return;
  }

  if (count_ == infinite || count_-- > 0) {
    func_();
    id_->first += interval_;
  } else {
    count_ = 0;
  }
}
} // namespace internal
} // namespace infgen
