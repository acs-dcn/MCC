#pragma once
#include "smp.h"
#include "log.h"
#include <tuple>

namespace infgen {

class no_sharded_instance_exception : std::exception {
  virtual const char *what() const noexcept override {
    return "sharded instance does not exist";
  }
};

/// Implement @Reducer concept, calculates the result by
/// adding elements to the  accumulator
class adder {
private:
  unsigned result_;
public:
  adder(unsigned initial=0): result_(initial) {}
  unsigned operator()(const unsigned& value) {
    result_ += value;
    return result_;
  }

  unsigned result() { return result_; }
  void reset() { result_ = 0; }
};

template <typename Service> class distributor {
  struct entry {
    std::shared_ptr<Service> service;
    bool freed;
  };
  std::vector<entry> instances_;
  size_t ended_services_{0};
  callback_t on_done_;

private:
  void service_deleted() { instances_[engine().cpu_id()].freed = true; }

public:
  distributor() {}
  ~distributor();

  /// Starts services by constructing an instance on every logical core
  /// with a copy of args passed to the constructor.
  ///
  /// \param args Arguments to be forwarded to \c Service constructor
  template <typename... Args> void start(Args &&... args);

  /// Starts services by constructing an instance on a single logical core
  /// with a copy of args passed to the constructor.
  ///
  /// \param args Arguments to be forwarded to service constructor
  template <typename... Args> void start_single(Args &&... args);

  /// Stop all started instances and destroys them.
  ///
  /// For every started instance, its stop() method is called, and then it is
  /// destroyed.
  void stop();

  /// Invoke a method on all instances of service.
  ///
  /// \param func member function to be called. Must return void.
  /// \param args arguments bo te passed to func
  template <typename... Args>
  void invoke_on_all(void (Service::*func)(Args...), Args &&... args);

  /// Invoke a callable object on all instances of Service.
  ///
  /// \param func a callable with the signature void(service&), to be called on
  /// each core with the local instance as in argument.
  template <typename Func> void invoke_on_all(Func &&func);

  /// Invoke a callable object on instance with designated id.
  ///
  /// \param id the id of the destination instance
  /// \param func a callable with the signature void(Service&), bo be called on
  ///        the core with id
  template <typename Func> void invoke_on(unsigned id, Func &&func);

  /// Set the on_done callback. The callback will be executed when all services
  /// has finished their work
  ///
  /// \param cb the lambda function definition of the callback
  void when_done(callback_t&& cb) {
    on_done_ = std::move(cb);
  }

  void end_game(Service* s);

  /*
  template <typename Reducer, typename Ret, typename... Args>
  inline Ret map_reduce(Reducer &&r, Ret (Service::*mapper)(Args... args),
                        Args &&... args) {
    for (unsigned i = 0; i < instances_.size(); i++) {
      smp::submit_to(i, [this, mapper, &r, args...] {
        auto inst = get_local_service();
        Ret partial = ((*inst).*mapper)(args...);
        r(partial);
        smp_logger.info("reduce: {} on {}", r.result(), partial);
      });
    }
    return r.result();
  }
  */

  template <typename... Args>
  inline void map_reduce(adder &r, uint64_t(Service::*mapper)(Args... args),
        Args&& ...args) {
    for (unsigned i = 1; i < instances_.size(); i++) {
      smp::submit_to(i, [this, mapper, &r, args...] {
        auto inst = get_local_service();
        auto partial = ((*inst).*mapper)(args...);
        return partial;
      }, [&r] (uint64_t partial) mutable {
          smp_logger.trace("reduce: {} on {}", r.result(), partial);
          r(partial);
      });
    }
  }


  const Service &local() const;
  Service &local();

private:
  template <typename... Args>
  std::shared_ptr<Service> create_local_service(Args &&... args) {
    auto s = std::make_shared<Service>(args...);
    s->set_container(this);
    return s;
  }

  std::shared_ptr<Service> get_local_service() {
    auto inst = instances_[engine().cpu_id()].service;
    if (!inst) {
      throw no_sharded_instance_exception();
    }
    return inst;
  }
};

template <typename Service> distributor<Service>::~distributor() {
  if (!instances_.empty()) {
    instances_.clear();
  }
}

template <typename Service>
template <typename... Args>
void distributor<Service>::start(Args &&... args) {
  instances_.resize(smp::count);
  for (unsigned c = 1; c < instances_.size(); c++) {
    smp::submit_to(c, [this, args = std::make_tuple(
                                 std::forward<Args>(args)...)]() mutable {
      instances_[engine().cpu_id()].service = std::apply(
          [this](Args... args) {
            return create_local_service(std::forward<Args>(args)...);
          },
          args);
    });
  }
}

template <typename Service>
template <typename... Args>
void distributor<Service>::start_single(Args &&... args) {
  assert(instances_.empty());
  instances_.resize(1);
  smp::submit_to(
      0, [this, args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
        instances_[0].service = std::apply(
            [this](Args... args) {
              create_local_service(std::forward<Args>(args)...);
            },
            args);
      });
}

template <typename Service>
template <typename... Args>
void distributor<Service>::invoke_on_all(void (Service::*func)(Args...),
                                         Args &&... args) {
  for (unsigned i = 1; i < instances_.size(); i++) {
    smp::submit_to(i, [this, func, args...] {
      auto inst = get_local_service();
      ((*inst).*func)(args...);
    });
  }
}

template <typename Service>
template <typename Func>
void distributor<Service>::invoke_on_all(Func &&func) {
  for (unsigned i = 1; i < instances_.size(); i++) {
    smp::submit_to(i, [this, func] {
      auto inst = get_local_service();
      func();
    });
  }
}

template <typename Service>
template <typename Func>
void distributor<Service>::invoke_on(unsigned id, Func &&func) {
  smp::submit_to(id, [this, func = std::forward<Func>(func)]() mutable {
    auto inst = get_local_service();
    func(*inst);
  });
}

template <typename Service>
void distributor<Service>::end_game(Service* s) {
  ended_services_++;
  if (ended_services_ >= instances_.size() - 1) {
    smp::submit_to(0, [this] {
      smp_logger.trace("all service finished");
      on_done_();
    });
  }
}

template <typename Service> void distributor<Service>::stop() {
  for (unsigned i = 1; i < instances_.size(); i++) {
    smp::submit_to(i, [this] {
      auto inst = instances_[engine().cpu_id()].service;
      if (!inst) {
        return;
      }
      instances_[engine().cpu_id()].service = nullptr;
      inst->stop();
    });
  }
}

} // namespace infgen
