#pragma once

#include <atomic>
#include <exception>
#include <mutex>
#include <string>
#include <unordered_map>
#include <map>
#include <cstdio>

#include <fmt/core.h>
#include <fmt/printf.h>
#include <fmt/time.h>

#include <syslog.h>

namespace infgen {

enum class log_level { error, warn, info, debug, trace };

std::ostream &operator<<(std::ostream &out, log_level level);
std::istream &operator>>(std::istream &in, log_level level);

class logger {
  std::string name_;
  std::atomic<log_level> level_ = {log_level::info};
  static std::atomic<bool> stdout_;
  static std::atomic<bool> syslog_;

  FILE* logfile_;

private:
  struct stringer {
    void (*append)(std::ostream &os, const void *object);
    const void *object;
  };

  template <typename Arg> stringer stringer_for(const Arg &arg) {
    return stringer{[](std::ostream &os, const void *object) {
                      os << *static_cast<const std::remove_reference_t<Arg> *>(
                          object);
                    },
                    &arg};
  }

  template <typename... Args>
  void do_log(log_level level, const char *fmt, Args &&... args);
  void really_do_log(log_level level, const char *fmt,
                     const stringer *stringers, size_t n);
  void failed_to_log(std::exception_ptr ex);

public:
  explicit logger(std::string name, bool log_file=false);
  logger(logger &&x);
  ~logger();

  static logger &get_logger();

  bool is_enabled(log_level level) const {
    return level <= level_.load(std::memory_order_relaxed);
  }

  template <typename... Args>
  void log(log_level level, const char *fmt, Args &&... args) {
    if (is_enabled(level)) {
      try {
        do_log(level, fmt, args...);
      } catch (std::exception &ex) {
        // failed_to_log(std::current_exception());
        fmt::print(ex.what());
      }
    }
  }

  template <typename... Args> void error(const char *fmt, Args &&... args) {
    log(log_level::error, fmt, std::forward<Args>(args)...);
		exit(1);
  }

  template <typename... Args> void warn(const char *fmt, Args &&... args) {
    log(log_level::warn, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args> void info(const char *fmt, Args &&... args) {
    log(log_level::info, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args> void debug(const char *fmt, Args &&... args) {
    log(log_level::debug, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args> void trace(const char *fmt, Args &&... args) {
    log(log_level::trace, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void fatalif(bool cond, const char *fmt, Args &&... args) {
    if (cond) {
      error(fmt, std::forward<Args>(args)...);
      exit(-1);
    }
  }

  const std::string &name() const { return name_; }

  log_level level() const { return level_; }

  void set_level(log_level new_level) {
    level_.store(new_level, std::memory_order_relaxed);
  }

  void set_level(std::string name) {
    std::map<std::string, log_level> level_map {
      { "info",  log_level::info  },
      { "error", log_level::error },
      { "debug", log_level::debug },
      { "trace", log_level::trace },
      { "warn",  log_level::warn  },
    };
    set_level(level_map[name]);
  }

  static void set_stdout_enabled(bool enabled);
  static void set_syslog_enabled(bool enabled);

  void set_logfile(bool enabled);
};

class logger_registry {
  mutable std::mutex mutex_;
  std::unordered_map<std::string, logger *> loggers_;

public:
  void set_all_logger_level(log_level level);
  void set_all_logger_level(std::string level);
  log_level get_logger_level(std::string name) const;
  void set_logger_level(std::string name, log_level level);
  void register_logger(logger *l);
  void unregister_logger(logger *l);
  void moved(logger *from, logger *to);
};

logger_registry &global_logger_registry();

template <typename... Args>
void logger::do_log(log_level level, const char *fmt, Args &&... args) {
  [&](auto... stringers) {
    stringer s[sizeof...(stringers)] = {stringers...};
    this->really_do_log(level, fmt, s, sizeof...(stringers));
  }(stringer_for<Args>(std::forward<Args>(args))...);
}

} // namespace infgen

namespace std {
std::ostream &operator<<(std::ostream &, const std::exception_ptr &);
std::ostream &operator<<(std::ostream &, const std::system_error &);
std::ostream &operator<<(std::ostream &, const std::exception &);
} // namespace std
