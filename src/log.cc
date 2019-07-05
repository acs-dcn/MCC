#include "log.h"
#include "reactor.h"


#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>

#include <fmt/time.h>
#include <fmt/color.h>

namespace infgen {

thread_local uint64_t logging_failures = 0;

std::atomic<bool> logger::stdout_ = {true};
std::atomic<bool> logger::syslog_ = {false};

logger::logger(std::string name, bool log_file) : name_(name), level_(log_level::info) {
  global_logger_registry().register_logger(this);
  set_logfile(log_file);
}

logger::logger(logger &&x)
    : name_(std::move(x.name_)),
      level_(x.level_.load(std::memory_order_relaxed)) {
  global_logger_registry().moved(&x, this);
}

logger::~logger() {
  global_logger_registry().unregister_logger(this);
  if (logfile_) {
    std::fclose(logfile_);
  }
}

logger &logger::get_logger() {
  static logger default_logger("default");
  logger::set_stdout_enabled(true);
  return default_logger;
}

void logger::failed_to_log(std::exception_ptr ex) {
  try {
    do_log(log_level::error, "failed to log message");
  } catch (...) {
    ++logging_failures;
  }
}

void logger::set_stdout_enabled(bool enabled) {
  stdout_.store(enabled, std::memory_order_relaxed);
}

void logger::set_syslog_enabled(bool enabled) {
  syslog_.store(enabled, std::memory_order_relaxed);
}

void logger::set_logfile(bool enabled) {
  if (enabled) {
    logfile_ = std::fopen(name_.c_str(), "w");
    if (!logfile_) {
      std::perror("file opending failed");
      exit(EXIT_FAILURE);
    }
  } else {
    logfile_ = nullptr;
  }
}

static void print_timestamp(std::ostream &os) {
  struct a_second {
    time_t t;
    std::string s;
  };
  static thread_local a_second this_second;
  using clock = std::chrono::high_resolution_clock;
  using namespace std::chrono_literals;
  auto n = clock::now();
  auto t = clock::to_time_t(n);
  if (this_second.t != t) {
    this_second.s = fmt::format("{:%Y-%m-%d %T}", fmt::localtime(t));
    this_second.t = t;
  }
  auto ms = (n - clock::from_time_t(t)) / 1ms;
  fmt::print(os, " {},{:03d}", this_second.s, ms);
}

void logger::really_do_log(log_level level, const char *fmt, const stringer *s,
                           size_t n) {
  bool is_stdout_enabled = stdout_.load(std::memory_order_relaxed);
  bool is_syslog_enabled = syslog_.load(std::memory_order_relaxed);

  if (!is_stdout_enabled && !is_syslog_enabled) {
    fmt::print("no log option is enabled!\n");
    return;
  }

  std::ostringstream out, log;

  static std::map<log_level, std::string> level_map = {
      {log_level::debug, "DEBUG"},
      {log_level::info, "INFO"},
      {log_level::trace, "TRACE"},
      {log_level::warn, "WARN"},
      {log_level::error, "ERROR"}};

  auto print_once = [&](std::ostream &out) {
    if (local_engine) {
      out << " [cpu " << std::setw(2) << engine().cpu_id() << "]" << name_ << " - ";
    } else {
      out << " " << name_ << " - ";
    }
    const char *p = fmt;
    while (*p != '\0') {
      if (*p == '{' && *(p + 1) == '}') {
        p += 2;
        if (n > 0) {
          s->append(out, s->object);
          ++s;
          --n;
        } else {
          out << "???";
        }
      } else {
        out << *p++;
      }
    }
    out << "\n";
  };

  if (is_stdout_enabled) {
    out << level_map[level];

    print_timestamp(out);
    print_once(out);
    if (logfile_) {
      fmt::print(logfile_, out.str());
    } else {
      if (level == log_level::error) {
        fmt::print(fg(fmt::terminal_color::red), out.str());
      } else {
        fmt::print(out.str());
      }
    }
  }

  if (is_syslog_enabled) {
    log << level_map[level];
    print_once(log);
    auto msg = log.str();
    syslog(int(level), "%s", msg.c_str());
  }


}

logger_registry &global_logger_registry() {
  static logger_registry g_registry;
  return g_registry;
}

void logger_registry::set_all_logger_level(log_level level) {
  std::lock_guard<std::mutex> gd(mutex_);
  for (auto &l : loggers_) {
    l.second->set_level(level);
  }
}

void logger_registry::set_all_logger_level(std::string level) {
  std::lock_guard<std::mutex> gd(mutex_);
  for (auto &l : loggers_) {
    l.second->set_level(level);
  }
}

log_level logger_registry::get_logger_level(std::string name) const {
  std::lock_guard<std::mutex> gd(mutex_);
  return loggers_.at(name)->level();
}

void logger_registry::set_logger_level(std::string name, log_level level) {
  std::lock_guard<std::mutex> gd(mutex_);
  loggers_.at(name)->set_level(level);
}

void logger_registry::register_logger(logger *l) {
  std::lock_guard<std::mutex> gd(mutex_);
  if (loggers_.find(l->name()) != loggers_.end()) {
    throw std::runtime_error("logger register twice");
  }
  loggers_[l->name()] = l;
}

void logger_registry::unregister_logger(logger *l) {
  std::lock_guard<std::mutex> gd(mutex_);
  loggers_.erase(l->name());
}

void logger_registry::moved(logger *from, logger *to) {
  std::lock_guard<std::mutex> gd(mutex_);
  loggers_[from->name()] = to;
}

} // namespace infgen

namespace std {
std::ostream &operator<<(std::ostream &out, std::exception_ptr &eptr) {
  if (!eptr) {
    out << "<no exception>";
    return out;
  }
  try {
    std::rethrow_exception(eptr);
  } catch (std::system_error &e) {
    out << " (error " << e.code() << ", " << e.code().message() << ")";
  } catch (std::exception &e) {
    out << " (" << e.what() << ")";
  } catch (...) {
  }
  return out;
}
} // namespace std
