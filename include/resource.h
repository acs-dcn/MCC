#pragma once

#include <vector>
#include <set>
#include <optional>

namespace infgen {
namespace resource {

using cpuset = std::set<unsigned>;

struct configuration {
  std::optional<size_t> cpus;
  std::optional<cpuset> cpu_set;
};

struct cpu {
  unsigned cpu_id;
};

struct resources {
  std::vector<cpu> cpus;
};

resources allocate(configuration c);
unsigned nr_processing_units();

} // namespace resource
} // namespace infgen
