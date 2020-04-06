#include "epoll.h"
#include "posix_connector.h"
#include "mtcp_connector.h"
#include "mepoll.h"
#include "log.h"
#include "reactor.h"
#include "smp.h"
#include "io_sched.h"
#include "task.h"
#include "resource.h"
#include <array>
#include <chrono>
#include <signal.h>

namespace infgen {

__thread reactor *local_engine;

logger net_logger("net");

reactor::signals::signals() : pending_signals_(0) {}

reactor::signals::~signals() {
  sigset_t mask;
  sigfillset(&mask);
  ::pthread_sigmask(SIG_BLOCK, &mask, NULL);
}

reactor::signals::signal_handler::signal_handler(int signo, std::function<void()>&& handler)
  : handler_(std::move(handler)) {
  struct sigaction sa;
  sa.sa_sigaction = action;
  sa.sa_mask = make_empty_sigset_mask();
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  auto r = ::sigaction(signo, &sa, nullptr);
  throw_system_error_on(r == -1, "sigaction");
  auto mask = make_sigset_mask(signo);
  r = ::pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
  throw_pthread_error(r);
}

void reactor::signals::handle_signal(int signo, std::function<void()>&& handler) {
  signal_handlers_.emplace(std::piecewise_construct,
      std::make_tuple(signo), std::make_tuple(signo, std::move(handler)));
}

void reactor::signals::handle_signal_once(int signo, std::function<void()>&& handler) {
  return handle_signal(signo, [fired = false, handler = std::move(handler)] () mutable {
    if (!fired) {
      fired = true;
      handler();
    }
  });
}

bool reactor::signals::poll_signal() {
  auto signals = pending_signals_.load(std::memory_order_relaxed);
  if (signals) {
    pending_signals_.fetch_and(~signals, std::memory_order_relaxed);
    for (size_t i = 0; i < sizeof(signals) * 8; i++) {
      if (signals & (1ull << i)) {
        signal_handlers_.at(i).handler_();
      }
    }
  }
  return signals;
}

void reactor::signals::action(int signo, siginfo_t* siginfo, void *ignore) {
  engine().signals_.pending_signals_.fetch_or(1ull << signo, std::memory_order_relaxed);
}

reactor::reactor(unsigned id) : id_(id) {}

connptr reactor::connect(socket_address sa, socket_address local) {
  auto conn = connector_->connect(sa, local);
  conns_.push_back(conn);
  return conn;
}

void reactor::reconnect(connptr conn) {
  connector_->reconnect(conn);
}

class reactor::poller::registration_task : public task {
private:
  poller *p_;

public:
  explicit registration_task(poller *p) : p_(p) {}
  virtual void run_and_dispose() noexcept override {
    if (p_) {
      engine().register_poller(p_->pollfn_.get());
      p_->registration_task_ = nullptr;
    }
  }
  void cancel() { p_ = nullptr; }
  void moved(poller *p) { p_ = p; }
};

void reactor::register_poller(pollfn *p) { pollers_.push_back(p); }

void reactor::unregister_poller(pollfn *p) {
  pollers_.erase(std::find(pollers_.begin(), pollers_.end(), p));
}

void reactor::register_queue(io_queue *q) {
  // wait for the I/O thread to start
  while (!watchdog);
  watchdog->add_queue(q);
}

reactor::poller::poller(poller &&x)
    : pollfn_(std::move(x.pollfn_)), registration_task_(x.registration_task_) {
  if (pollfn_ && registration_task_) {
    registration_task_->moved(this);
  }
}

reactor::poller::~poller() {
  if (pollfn_) {
    if (registration_task_) {
      registration_task_->cancel();
    } else {
      engine().unregister_poller(pollfn_.get());
    }
  }
}

reactor::poller &reactor::poller::operator=(poller &&x) {
  if (this != &x) {
    this->~poller();
    new (this) poller(std::move(x));
  }
  return *this;
}
void reactor::poller::do_register() {
  auto task = std::make_unique<registration_task>(this);
  auto tmp = task.get();
  engine().add_task(std::move(task));
  registration_task_ = tmp;
}


void reactor::execute_tasks() {
  do {
    auto t = std::move(task_queue_.front());
    t->run_and_dispose();
    task_queue_.pop();
  } while (has_pending_task());
}

class reactor::epoll_pollfn final : public reactor::pollfn {
  reactor &r_;

public:
  epoll_pollfn(reactor &r) : r_(r) {}
  virtual bool poll() final override { return r_.poll_io(); }
};

class reactor::smp_pollfn final : public reactor::pollfn {
  reactor &r_;

public:
  smp_pollfn(reactor &r) : r_(r) {}
  virtual bool poll() final override { return smp::poll_queues(); }
};

class reactor::mtcp_pollfn final : public reactor::pollfn {
  reactor &r_;

public:
  mtcp_pollfn(reactor &r) : r_(r) {}
  virtual bool poll() final override { return r_.poll_io(); }
};

class reactor::signal_pollfn final : public reactor::pollfn {
  reactor &r_;


public:
  signal_pollfn(reactor &r) : r_(r) {}
  virtual bool poll() final override { return r_.signals_.poll_signal(); }
};



void reactor::start_epoll() {
  if (!epoll_poller_) {
    net_logger.trace("start epoll poller on core {}", id_);
    epoll_poller_ = poller(std::make_unique<epoll_pollfn>(*this));
  }
}

void reactor::start_mtcp_epoll() {
  if (!epoll_poller_) {
    epoll_poller_ = poller(std::make_unique<mtcp_pollfn>(*this));
    net_logger.trace("start mtcp poller on core {}", id_);
  }
}

void reactor::run() {
  net_logger.info("engine {} running...", engine().cpu_id());

  //signal(SIGPIPE, SIG_IGN);

  std::optional<poller> smp_poller = {};

  if (smp::count > 1) {
    smp_poller = poller(std::make_unique<smp_pollfn>(*this));
  }

  poller signal_poller = poller(std::make_unique<signal_pollfn>(*this));

  if (id_ == 0)  {
    signals_.handle_signal_once(SIGINT, [this] {
      net_logger.warn("SIGINT signal fired!");
      stop();
    });

    signals_.handle_signal_once(SIGTERM, [this] {
      net_logger.warn("SIGTERM signal fired!");
      stop();
    });
  }

  // Check out events from poller set.
  // The default poller set includes:
  //  smp_poller (when using multi-core)
  //  signal_poller (constructed by default)
  //  epoll_poller (when the first epoll event is added)
  std::function<bool()> check_for_events = [this]() {
    return poll_once() || has_pending_task();
  };

  while (!stopping_) {
    // check out tasks from timer
    tm_.tick();

    // check out tasks from task waiting list
    if (has_pending_task()) {
      execute_tasks();
    }
    auto start = system_clock::now();
    if (check_for_events()) {
      auto end = system_clock::now();
      auto diff = duration_cast<nanoseconds>(end - start);
      net_logger.trace("poll and process {} ns", diff.count());
    }
  }
  // close connections
  for (auto& c: conns_) {
    if (c->get_state() == tcp_connection::state::connected) {
      //net_logger.info("conn size: {}, closing {} ", conns_.size(), c->get_id());
      c->close();
    }
  }

  // flush remaing requests one last time
  // may be we can use this chance to close all remaing streams?
  //poll_once();

  if (mctx_) {
    sleep(conns_.size() / 200000);
  }

  // I/O thread must be stopped after connection is closed
  // to get the close message sent
  if (network_stack_ == "mtcp" && mode_ != "normal") {
    watchdog->stop();
  }

  if (mctx_) {
    mtcp_destroy_context(mctx_);
  }

  if (id_ == 0) {
    net_logger.info("engine {} stopped, waiting for workers", id_);
    smp::join_all();
  } else {
    net_logger.info("engine {} stopped", id_);
  }
}

void reactor::stop() {
  if (id_ == 0) {
    for (unsigned i = 1; i < smp::count; i++) {
      smp::submit_to(i, [] {
        engine().stop();
      });
    }
  }
  stopping_  = true;
  net_logger.trace("engine {} stopping...", id_);
}

void reactor::configure(boost::program_options::variables_map configuration) {
  network_stack_ = configuration["network-stack"].as<std::string>();
  mode_ = configuration["mode"].as<std::string>();
	// @wuwenqing
	ssl_layer::ssl_init(sctx_);

  if (network_stack_ == "kernel") {
    pin_this_thread(id_);
    if (!configuration.count("device")) {
      net_logger.error("config error: a network device must be assigned"
                       "when using kernel stack!\n");
      exit(-1);
    }
    mctx_ = nullptr;
    connector_ = std::make_unique<posix_connector>();
    backend_ = std::make_unique<epoll_backend>();
    connector_->configure(configuration);
  } else if (network_stack_ == "mtcp") {
    if (smp::count > 1 && id_ == 0) {
    // distributor thread in multi-threaded environment
      pin_this_thread(smp::count-1);
      mctx_ = nullptr;
      connector_ = std::make_unique<posix_connector>();
      backend_ = std::make_unique<epoll_backend>();
      connector_->configure(configuration);
    } else {
      unsigned core = (id_ == 0 ? 0 : id_ - 1);
      stack_ = std::make_unique<mtcp_stack>();
      stack_->create_stack_thread(core);
      mctx_ = stack_->context();
      net_logger.info("Stack thread {} started on core {}", id_, core);

      if (mode_ != "normal") {
        io_queue_ = std::make_unique<io_queue>();
      } else {
        io_queue_ = nullptr;
      }
      // pin app thread to the same physical core
      pin_this_thread(core + resource::nr_processing_units() / 2);
      connector_ = std::make_unique<mtcp_connector>();
      backend_ = std::make_unique<mtcp_epoll_backend>();
      connector_->configure(configuration);
    }

    ready_ = true;
  }

}

bool reactor::poll_once() {
  bool work = false;
  for (auto c : pollers_) {
    work |= c->poll();
  }
  return work;
}

boost::program_options::options_description
reactor::get_options_description() {
  namespace bpo = boost::program_options;
  bpo::options_description opts("Net options");
  opts.add_options()
    ("network-stack", bpo::value<std::string>()->default_value("kernel"),
                      "select network stack (default: kernel stack")
    ("device", bpo::value<std::string>(),
     "select which network device to use (only avaiable when using kernel stack)")
    ("ips", bpo::value<int>()->default_value(200), "number of ips when using mtcp stack")
    ("no-delay", bpo::value<bool>()->default_value(false), "forbid tcp naggle")
    ("dest", bpo::value<std::string>()->default_value("192.168.1.1"), "destination ip");
  return opts;
}

} // namespace infgen
