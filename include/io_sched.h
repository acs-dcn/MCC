#pragma once

#include "reactor.h"
#include <string>

inline bool likely(bool expr) {
#ifdef __GNUC__
    return __builtin_expect(expr, true);
#else
    return expr;
#endif
}

/** \brief hint for the branch prediction */
inline bool unlikely(bool expr) {
#ifdef __GNUC__
    return __builtin_expect(expr, false);
#else
    return expr;
#endif
}

namespace infgen {

class io_queue {
private:
  struct config {};
  reactor &r_;

public:
  io_queue() : r_(engine()) { r_.register_queue(this); }
  io_queue(reactor &r) : r_(r) { r_.register_queue(this); }
  size_t capacity() const { return 0; }
  size_t queued_packets() const { return 0; }

  int send_packets() { return r_.flush_packets(); }
  int burst_packets() { return r_.burst_packets(); }
};

class io_scheduler {
private:
  bool burst_mode_ { false };
  struct burst_config {
    uint64_t burst_on;
    uint64_t burst_off;
  };

  burst_config burst_config_;
  std::vector<std::thread> burst_threads_;

  bool precision_mode_ { false };
  struct timer_config {
    uint64_t queue_id;
    uint64_t one_round_length;
    std::vector<int> timestamps;
  };
  timer_config timer_config_;

  unsigned cpu_id_;
  unsigned nr_queue_;
  bool stop_;

  std::vector<io_queue *> io_queues_;
  void set_mode(std::string mode);
  void create_burst_thread(std::function<void()> do_burst);

public:
  io_scheduler(std::string mode, int cpu_id);
  static boost::program_options::options_description get_options_description();
  void configure(boost::program_options::variables_map vm);

  void add_queue(io_queue *q) {
    io_queues_.push_back(q);
    nr_queue_++;
  }

  void run();
  void stop();
  void io_loop();
  void burst_loop(unsigned cpu_id);
  int nr_queue() { return nr_queue_; }

  static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
  }

  static inline uint64_t us_to_tsc(int us) { return 0; }

private:
  void pick_queue(uint64_t id);
};

extern io_scheduler *watchdog;
} // namespace infgen
