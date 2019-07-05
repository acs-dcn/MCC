#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <memory>

namespace infgen {

using namespace std::chrono;
using namespace std::chrono_literals;

using timer_id = std::shared_ptr<std::pair<system_clock::time_point, uint64_t>>;

constexpr int infinite = -1;

namespace internal {

// Manage timed events in reactor loop
//  A tick method is invoked in each iteration of eventloop to checkout expired timmers
//  And user-level API is defined here to allow app to set timed events
class timer_manager {
public:
  timer_manager() = default;
  ~timer_manager() = default;
  timer_manager(const timer_manager &) = delete;
  void operator=(const timer_manager &) = delete;

  int iteration;

  void tick();
  size_t size();
  bool cancel(timer_id id);

  microseconds latest_timeout() const;

  // Schedule a timed event described by a 2-tuple (time, func), this func will
  // be executed at time and a repeat number is provided to allow multiple executions.
  template <int RepeatCount, typename Duration, typename Func>
  timer_id schedule_at_with_repeat(const system_clock::time_point &trigger_time,
                                   const Duration &peroid, Func &&f);

  template <int RepeatCount, typename Duration, typename Func>
  timer_id schedule_after_with_repeat(const Duration &duration, Func &&f);

  template <typename Func>
  timer_id schedule_at(const system_clock::time_point &trigger_time, Func &&f);

  template <typename Duration, typename Func>
  timer_id schedule_after(const Duration &duration, Func &&f);

private:
  // Interval clss to provide the actual timer container and callback settings,
  // in our case the container is a std::multimap, which is implemented using
  // red-black tree.
  class timer {
  public:
    explicit timer(const system_clock::time_point &tp);
    // copy a timer is not allowed
    timer(const timer &) = delete;
    void operator=(const timer &) = delete;

    timer(timer &&t);
    timer &operator=(timer &&t);

    timer_id id() const { return id_; }

  private:
    using callback_t = std::function<void()>;
    void set_callback(callback_t &&cb) { func_ = std::move(cb); }
    void alarm();

    callback_t func_;
    timer_id id_;
    microseconds interval_;
    int count_;

    friend class timer_manager;
  };

  std::multimap<system_clock::time_point, timer> timers_;
  static uint64_t g_timer_id;
};

template <int RepeatCount, typename Duration, typename Func>
timer_id timer_manager::schedule_at_with_repeat(const system_clock::time_point &trigger_time,
                                                const Duration &peroid,
                                                Func &&f) {
  static_assert(RepeatCount != 0, "timer count must be non-zero!");
  timer t(trigger_time);
  t.interval_ =
      std::max(microseconds(1), std::chrono::duration_cast<microseconds>(peroid));
  t.count_ = RepeatCount;
  timer_id id = t.id();
  t.set_callback(std::forward<Func>(f));
  timers_.insert(std::make_pair(trigger_time, std::move(t)));
  return id;
}

template <int RepeatCount, typename Duration, typename Func>
timer_id timer_manager::schedule_after_with_repeat(const Duration &duration,
                                                   Func &&f) {
  const auto now = system_clock::now();
  return schedule_at_with_repeat<RepeatCount>(now + duration, duration,
                                              std::forward<Func>(f));
}

template <typename Func>
timer_id timer_manager::schedule_at(const system_clock::time_point &trigger_time, Func &&f) {
  return schedule_at_with_repeat<1>(trigger_time, microseconds(0),
                                    std::forward<Func>(f));
}

template <typename Duration, typename Func>
timer_id timer_manager::schedule_after(const Duration &duration, Func &&f) {
  const auto now = system_clock::now();
  return schedule_at(now + duration, std::forward<Func>(f));
}

} // namespace internal
} // namespace infgen
