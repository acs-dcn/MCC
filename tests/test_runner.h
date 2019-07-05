#pragma once

#include <functional>
#include <atomic>

namespace infgen {

class test_runner {
  std::atomic<bool> started_{false};
  bool done_ = false;

public:
  void start(int argc, char **argv);
  ~test_runner();
  void run_sync(std::function<void()> task);
};

test_runner &global_test_runner();

} // namespace infgen
