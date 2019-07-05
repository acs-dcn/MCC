#include "resource.h"
#include "smp.h"
#include "thread.h"
#include "task.h"
#include "io_sched.h"

#include "mtcp_api.h"

namespace infgen {

logger smp_logger("smp");

void smp_message_queue::lf_queue::maybe_wakeup() {}

void smp_message_queue::move_pending() {
  auto begin = pending_fifo_.cbegin();
  auto end = pending_fifo_.cend();
  end = pending_.push(begin, end);
  if (begin == end) {
    return;
  }
  pending_.maybe_wakeup();
  pending_fifo_.erase(begin, end);
}

void smp_message_queue::flush_response_batch() {
  if (!completed_fifo_.empty()) {
    auto begin = completed_fifo_.cbegin();
    auto end = completed_fifo_.cend();
    end = completed_.push(begin, end);
    if (begin == end) {
      return;
    }
    completed_.maybe_wakeup();
    completed_fifo_.erase(begin, end);
  }
}

void smp_message_queue::flush_request_batch() {
  if (!pending_fifo_.empty()) {
    move_pending();
  }
}

void smp_message_queue::submit_item(std::unique_ptr<func_wrapper> func) {
  pending_fifo_.push_back(func.get());
  func.release();
  if (pending_fifo_.size() >= batch_size) {
    move_pending();
  }
}

void smp_message_queue::respond(func_wrapper *func) {
  completed_fifo_.push_back(func);
  if (completed_fifo_.size() >= batch_size || engine().stopped_) {
    flush_response_batch();
  }
}

template <typename Func>
size_t smp_message_queue::process_queue(lf_queue &q, Func process) {
  func_wrapper *func;
  func_wrapper *items[queue_length];

  if (!q.pop(func)) {
    return 0;
  }
  auto nr = q.pop(items);
  unsigned i = 0;

  do {
    process(func);
    func = items[i++];
  } while (i <= nr);
  return nr + 1;
}

size_t smp_message_queue::process_incoming() {
  auto nr = process_queue(pending_, [](func_wrapper *func) { func->process(); });
  return nr;
}

size_t smp_message_queue::process_completions() {
  auto nr = process_queue(completed_, [](func_wrapper *func) {
    func->complete();
    delete func;
  });
  return nr;
}

thread_local std::unique_ptr<reactor> reactor_holder;
std::vector<std::thread> smp::threads_;
std::vector<reactor *> smp::reactors_;
std::thread::id smp::tmain_;
std::deque<std::deque<smp_message_queue>> smp::qs_;
std::atomic<unsigned> smp::ready_engines_(1);
unsigned smp::count = 1;

boost::program_options::options_description smp::get_options_description() {
  namespace bpo = boost::program_options;
  bpo::options_description opts("SMP options");
  opts.add_options()
    ("smp", bpo::value<unsigned>()->default_value(1), "number of threads (default: one per CPU)")
    ("mode", bpo::value<std::string>()->default_value("normal"), "I/O mode")
  ;
  return opts;
}

void smp::configure(boost::program_options::variables_map configuration) {
  smp::count = 1;
  smp::tmain_ = std::this_thread::get_id();
  if (configuration.count("smp")) {
    smp::count = configuration["smp"].as<unsigned>();
  }
  reactors_.resize(smp::count);

  allocate_reactor(0);
  reactors_[0] = &engine();
  smp::ready_engines_ = 1;

  smp::qs_.resize(smp::count);
  for (unsigned i = 0; i < smp::count; i++) {
    for (unsigned j = 0; j < smp::count; j++)
      smp::qs_[i].emplace_back(reactors_[i], reactors_[j]);
  }
  // use extra I/O scheduler when using mtcp stack
  auto stack = configuration["network-stack"].as<std::string>();
  if (stack == "mtcp") {
    // must initialize mtcp before starting any I/O threads!!!
    mtcp_stack::configure(configuration);
    auto mode = configuration["mode"].as<std::string>();
    if (mode != "normal") {
      // extra I/O thread
      use_extra_io = 1;
      watchdog = new io_scheduler(mode, smp::count);
      watchdog->configure(configuration);
      smp::create_thread([] {
        // pin I/O thread to the first cpu core after the stack thread
        pin(smp::count);
        watchdog->run();
      });
    }
  }

  for (unsigned i = 1; i < smp::count; i++) {
    smp::create_thread([=] {
      allocate_reactor(i);
      reactors_[i] = &engine();
      // we leave pin_thread operation on reactor itself.
      engine().configure(configuration);
      smp::ready_engines_.fetch_add(1, std::memory_order_relaxed);
      engine().run();
    });
  }
  smp_logger.trace("waiting for all engines to start...");
  while (!ready());
  smp_logger.trace("engine check ok!");
  engine().configure(configuration);
}

bool smp::poll_queues() {
  size_t got = 0;
  for (unsigned i = 0; i < smp::count; i++) {
    if (engine().cpu_id() != i) {
      // rxq stores tasks suppposed to be processed on this core
      auto &rxq = qs_[engine().cpu_id()][i];
      rxq.flush_response_batch();
      // process the incoming tasks, stores the result to rxq
      got += rxq.process_incoming();
      // txq stores tasks commited by this core and will be processed
      // on the destination core
      auto &txq = qs_[i][engine().cpu_id()];
      txq.flush_request_batch();

      got += txq.process_completions();
    }
  }

  return got != 0;
}

void smp::allocate_reactor(unsigned id) {
  assert(!reactor_holder);
  local_engine = new reactor(id);
  reactor_holder.reset(local_engine);
}

void smp::create_thread(std::function<void()> thread_loop) {
  threads_.emplace_back(std::move(thread_loop));
}

void smp::cleanup() {
  smp::threads_ = std::vector<std::thread>();
  //watchdog->stop();
}

void smp::join_all() {
  for (auto &&t : threads_) {
    t.join();
  }
}

void smp::pin(unsigned cpu_id) { pin_this_thread(cpu_id); }

} // namespace infgen
