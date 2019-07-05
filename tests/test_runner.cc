#include "test_runner.h"

void test_runner::start(int argc, char **argv) {
  bool expected = false;
  if (!started_.compare_and_exchange_strong(expected, true,
                                            std::memory_order_acquire)) {
    return;
  }

  application app;
  app.run(ac, av, [this] {});
}

