#pragma once
#include <boost/lockfree/spsc_queue.hpp>
#include <functional>
#include <tuple>

#include "reactor.h"
#include "log.h"

namespace infgen {

using callback_t = std::function<void()>;

extern logger smp_logger;
//
class smp_message_queue {
  static constexpr size_t queue_length = 128;
  static constexpr size_t batch_size = 1;
  struct func_wrapper;
  struct lf_queue_remote {
    reactor *remote;
  };
  using lf_queue_base =
      boost::lockfree::spsc_queue<func_wrapper *,
                                  boost::lockfree::capacity<queue_length>>;
  struct lf_queue : lf_queue_base, lf_queue_remote {
    lf_queue(reactor *remote) : lf_queue_remote{remote} {}
    void maybe_wakeup();
  };

  lf_queue pending_;
  lf_queue completed_;

  struct func_wrapper {
    virtual ~func_wrapper() {}
    virtual void process() = 0;
    virtual void complete() = 0;
  };

  template <typename Func> struct async_pure_func : func_wrapper {
  private:
    smp_message_queue &queue_;
    Func func_;
    callback_t on_completed_;
    bool has_callback_{false};

  public:
    async_pure_func(smp_message_queue &q, Func &&f)
        : queue_(q), func_(std::forward<Func>(f)) {}
    async_pure_func(smp_message_queue &q, Func &&f, callback_t &&cb)
        : queue_(q), func_(std::forward<Func>(f)), on_completed_(std::move(cb)),
          has_callback_(true) {}
    virtual void process() override {
      // Note: if the task is a member function of some class, then the func
      // will create a local instance and execute this member function
      func_();
      // item is moved to the complete queue, the completion callback must be
      // polled by the caller core
      queue_.respond(this);
    }
    virtual void complete() override {
      if (has_callback_) {
        on_completed_();
      }
    }
  };

  template <typename Func, typename T> struct async_func : func_wrapper {
  private:
    smp_message_queue &queue_;
    Func func_;
    T result_;
    std::function<void(T)> on_completed_;
    bool has_callback_{false};

  public:
    async_func(smp_message_queue &q, Func &&f)
        : queue_(q), func_(std::forward<Func>(f)) {}
    async_func(smp_message_queue &q, Func &&f,
                       std::function<void(T)> &&cb)
        : queue_(q), func_(std::forward<Func>(f)), on_completed_(std::move(cb)),
          has_callback_(true) {}

    virtual void process() override {
      result_ = func_();
      queue_.respond(this);
    }

    virtual void complete() override {
      if (has_callback_) {
        on_completed_(result_);
      }
    }
  };
  /// A temporary FIFO buffer for batch processing.
  /// The constructed work item will be pushed into this area directly.
  /// When the size of this buffer reach a threashold, items will be moved
  /// to the actual message queue.
  ///
  /// pending: tasks waiting to be processed
  std::vector<func_wrapper *> pending_fifo_;
  /// completed: result of tasks that has been processed
  std::vector<func_wrapper *> completed_fifo_;

public:
  smp_message_queue(reactor *from, reactor *to)
      : pending_(to), completed_(from) {}
  ~smp_message_queue() {}

  template <typename Func> void submit(Func &&func) {
    auto async_f = std::make_unique<async_pure_func<Func>>(*this,
                                                      std::forward<Func>(func));
    submit_item(std::move(async_f));
  }

  template <typename Func> void submit(Func &&func, callback_t &&cb) {
    auto async_f = std::make_unique<async_pure_func<Func>>(
        *this, std::forward<Func>(func), std::move(cb));
    submit_item(std::move(async_f));
  }

  template <typename Func, typename T>
  void submit(Func&& func, std::function<void(T)>&& cb) {
    auto async_f = std::make_unique<async_func<Func, T>>(
        *this, std::forward<Func>(func), std::move(cb));
    submit_item(std::move(async_f));
  }

  void start(unsigned cpuid);
  void stop();

  size_t process_completions();
  size_t process_incoming();

  /// Move the completed work items staged in the FIFO queue
  /// to the 'complete' message queue
  void flush_response_batch();

  /// Move the
  void flush_request_batch();

private:
  void work();
  void move_pending();
  void submit_item(std::unique_ptr<func_wrapper> func);
  void respond(func_wrapper *func);
  template <typename Func> size_t process_queue(lf_queue &q, Func process);
};

class smp {
  static std::vector<std::thread> threads_;
  static std::vector<reactor *> reactors_;
  static std::thread::id tmain_;
  static std::atomic<unsigned> ready_engines_;
  // use deque instead of vector to avoid memory relocation
  static std::deque<std::deque<smp_message_queue>> qs_;

public:
  static boost::program_options::options_description get_options_description();
  static void configure(boost::program_options::variables_map vm);
  static void cleanup();
  static void join_all();
  static bool main_thread() { return std::this_thread::get_id() == tmain_; }
  static bool ready() {
    auto engines = ready_engines_.load(std::memory_order_relaxed);
    return engines >= smp::count;
  }

  template <typename Func> static void submit_to(unsigned t, Func &&func) {
    if (t == engine().cpu_id()) {
      func();
    } else {
      qs_[t][engine().cpu_id()].submit(std::forward<Func>(func));
    }
  }

  template <typename Func>
  static void submit_to(unsigned t, Func &&func, callback_t &&cb) {
    if (t == engine().cpu_id()) {
      func();
      cb();
    } else {
      qs_[t][engine().cpu_id()].submit(std::forward<Func>(func), std::move(cb));
    }
  }

  template <typename Func>
  static void submit_to(unsigned t, Func &&func, std::function<void(unsigned)>&& cb) {
    if (t == engine().cpu_id()) {
      func();
    } else {
      qs_[t][engine().cpu_id()].submit(std::forward<Func>(func), std::move(cb));
    }
  }

  static bool poll_queues();

private:
  static void pin(unsigned cpu_id);
  static void allocate_reactor(unsigned cpu_id);
  static void create_thread(std::function<void()> thread_loop);

public:
  static unsigned count;
};
} // namespace infgen
