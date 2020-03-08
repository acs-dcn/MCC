#pragma once

#include <cassert>
#include <iostream>
#include <queue>
#include <thread>
#include <atomic>

#include "connection.h"
#include "mtcp_stack.h"
#include "thread.h"
#include "timer.h"
//@wuwenqing
#include "ssl_layer.h"
#include <boost/program_options.hpp>

#include "mtcp_api.h"

namespace infgen {

class poll_state;
class backend {
public:
  virtual ~backend() {}
  virtual bool poll(int timeout) = 0;
  virtual void update(poll_state &s, int event) = 0;
  virtual void forget(poll_state &) = 0;
};

class task;
class io_queue;

class reactor {
private:
  struct pollfn {
    virtual ~pollfn() {}
    // Returns true if work is done (false = idle)
    virtual bool poll() = 0;
    // Checks if work needs to be done, but without actually doing any
    // returns true if works need to be done ( false = idle)
    // virtual bool pure_poll() = 0;
  };

  class smp_pollfn;
  class epoll_pollfn;
  class mtcp_pollfn;
  class signal_pollfn;

  void register_poller(pollfn *p);
  void unregister_poller(pollfn *p);

  class signals {
  public:
    signals();
    ~signals();

    bool poll_signal();
    void handle_signal(int signo, std::function<void()> &&handler);
    void handle_signal_once(int signo, std::function<void()> &&handler);
    static void action(int signo, siginfo_t *siginfo, void *ignore);
    std::atomic<uint64_t> pending_signals_;

  private:
    struct signal_handler {
      signal_handler(int signo, std::function<void()> &&handler);
      std::function<void()> handler_;
    };
    std::unordered_map<int, signal_handler> signal_handlers_;
  };

  signals signals_;
  std::vector<std::shared_ptr<tcp_connection>> conns_;

public:
  class poller {
    std::unique_ptr<pollfn> pollfn_;
    class registration_task;
    registration_task *registration_task_;

  public:
    poller(std::unique_ptr<pollfn> fn) : pollfn_(std::move(fn)) {
      do_register();
    }
    ~poller();
    poller(poller &&x);
    poller &operator=(poller &&x);
    void do_register();
  };

private:
  std::vector<pollfn *> pollers_;

public:
  reactor() = default;
  reactor(const reactor &) = delete;
  void operator=(const reactor &) = delete;
  explicit reactor(unsigned id);
  void run();
  void stop();
  unsigned cpu_id() { return id_; }
  static boost::program_options::options_description get_options_description();
  bool stopped_;

  bool mtcp_bind_;

public:
  /// Timer related APIs.
  template <int RepeatCount, typename Duration, typename Func>
  void add_periodic_task_at(const system_clock::time_point &trigger_time,
                            const Duration &peroid, Func &&f) {
    tm_.schedule_at_with_repeat<RepeatCount>(trigger_time, peroid,
                                             std::forward<Func>(f));
  }

  template <int RepeatCount, typename Duration, typename Func>
  void add_periodic_task_after(const Duration &duration, Func &&f) {
    tm_.schedule_after_with_repeat<RepeatCount>(duration,
                                                std::forward<Func>(f));
  }

  template <typename Func>
  void add_oneshot_task_at(const system_clock::time_point &trigger_time, Func &&f) {
    tm_.schedule_at(trigger_time, std::forward<Func>(f));
  }

  template <typename Duration, typename Func>
  void add_oneshot_task_after(const Duration &duration, Func &&f) {
    tm_.schedule_after(duration, std::forward<Func>(f));
  }

  /// mTCP Timer related APIs.

  template <typename Func>
  void run_at(uint64_t milli, Func &&f, uint64_t interval = 0);

  template <typename Func>
  void run_after(uint64_t milli, Func &&f, uint64_t interval = 0);

  /// Connection related APIs
  connptr connect(socket_address sa, socket_address local = socket_address{});
  void reconnect(connptr conn);

  /// Event I/O related APIs
  void start_epoll();
  void start_mtcp_epoll();
  void update(poll_state &s, int event) { backend_->update(s, event); }
  bool poll_io(int timeout = 0) { return backend_->poll(timeout); }
  void forget(poll_state &ch) { backend_->forget(ch); }

  // packet I/O related APIs
  void register_queue(io_queue *q);
  int flush_packets() { return stack_->send_packets(); }
  int burst_packets() { return stack_->burst_packets(); }

  bool ready() { return ready_; }

private:
  bool stopping_ { false };
  bool ready_ { false };
  unsigned id_;
  internal::timer_manager tm_;
  std::queue<std::unique_ptr<task>> task_queue_;
  std::unique_ptr<backend> backend_;
  std::unique_ptr<connector> connector_;
  std::optional<poller> epoll_poller_{};
  friend class smp;

  std::string network_stack_;
  std::string mode_;

  std::unique_ptr<mtcp_stack> stack_;
  std::unique_ptr<io_queue> io_queue_;
  // mtcp context, every context is bound to one cpu core
  mctx_t mctx_;
	//@ wuwenqing, for encryption
	mbedtls_gcm_context sctx_;

public:
  mctx_t context() { return mctx_; }
	mbedtls_gcm_context ssl_context() { return sctx_; }

public:
  void add_task(std::unique_ptr<task> &&t) { task_queue_.push(std::move(t)); }

private:
  bool has_pending_task() { return !task_queue_.empty(); }
  void execute_tasks();
  bool poll_once();
  void configure(boost::program_options::variables_map vm);
};

extern __thread reactor *local_engine;

inline reactor &engine() { return *local_engine; }

inline bool engine_is_ready() { return local_engine != nullptr && local_engine->ready(); }

} // namespace infgen
