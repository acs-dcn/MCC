#pragma once
#include "mtcp_api.h"
#include <iostream>

namespace infgen {

class mtcp_stack {

private:
  mctx_t mctx_;
  unsigned cpu_id_;

public:
  mctx_t context() { return mctx_; }

  unsigned cpu() { return cpu_id_; }

  static void set_stack_cpus(unsigned n) {
    mtcp_conf mcfg;
    mtcp_getconf(&mcfg);
    mcfg.num_cores = n;
    mtcp_setconf(&mcfg);
  }

  static void configure(boost::program_options::variables_map vm) {
    auto nr_cpus = vm["smp"].as<unsigned>();
    
    set_stack_cpus(nr_cpus == 1? 1: nr_cpus-1);

    auto ret = mtcp_init("config/mtcp.conf");
    if (ret < 0) {
      exit(-1);
    }
  }

  void create_stack_thread(unsigned id) {
    cpu_id_ = id;
    // create a mtcp stack thread
    mctx_ = mtcp_create_context(id);
  }

  int send_packets() {
    auto ret = mtcp_io_flush(mctx_);
    return ret;
  }

  int burst_packets() {
    auto ret = mtcp_burst_packets(mctx_);
    return ret;
  }
};
} // namespace infgen
