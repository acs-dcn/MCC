#include "resource.h"
#include <unistd.h>

namespace infgen {

namespace resource {

resources allocate(configuration c) {
  resources ret;
  auto cpuset_procs = c.cpu_set ? c.cpu_set->size() : nr_processing_units();
  auto procs = c.cpus.value_or(cpuset_procs);
  ret.cpus.reserve(procs);
  for (unsigned i = 0; i < procs; i++) {
    ret.cpus.push_back(cpu{i});
  }
  return ret;
}
unsigned nr_processing_units() { return ::sysconf(_SC_NPROCESSORS_ONLN); }
} // namespace resource
} // namespace infgen
