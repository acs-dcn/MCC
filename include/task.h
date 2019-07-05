#pragma once
#include <memory>

namespace infgen {

class task {
public:
  virtual ~task() noexcept {}
  virtual void run_and_dispose() noexcept = 0;
};
} // namespace infgen
