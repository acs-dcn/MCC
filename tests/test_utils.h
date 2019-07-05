#pragma once

namespace infgen {

class infgen_test {
public:
  infgen_test();
  virtual ~infgen_test() {}
  virtual void run_test_case() = 0;
  void run();
};

#define INFGEN_TEST_CASE(name) \
  struct name : public infgen_test { \
    void run_test_case() override; \
  }; \
  static name name ## instance_; \
  void name::run_test_case()


