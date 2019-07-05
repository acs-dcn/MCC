#include "io_sched.h"
#include "log.h"
#include "smp.h"

#include <fstream>
#include <iostream>
#include <fstream>
#include <array>

namespace infgen {

io_scheduler *watchdog;
logger io_logger("IO");

void io_scheduler::set_mode(std::string mode) {
  if (mode == "burst") {
    burst_mode_ = true;
  } else if (mode == "precision") {
    precision_mode_ = true;
  } else {
    std::cerr << "unknown parameter" << std::endl;
    exit(1);
  }
}

void io_scheduler::pick_queue(uint64_t id) {
  timer_config_.queue_id = id;
}

io_scheduler::io_scheduler(std::string mode, int cpu_id): cpu_id_(cpu_id) {
  set_mode(mode);
  if (mode == "precision") {
    pick_queue(0);
  }
}

boost::program_options::options_description
io_scheduler::get_options_description() {
  namespace bpo = boost::program_options;
  bpo::options_description opts("IO options");
  opts.add_options()
    ("traffic-model", bpo::value<std::string>()->default_value("model.txt"), "traffic model file")
    ("burst-on", bpo::value<unsigned>()->default_value(100), "burst traffic last time")
    ("burst-off", bpo::value<unsigned>()->default_value(900), "burst traffic break time")
  ;
  return opts;
}

void io_scheduler::configure(boost::program_options::variables_map vm) {
  if (vm.count("traffic-model")) {
    auto model = vm["traffic-model"].as<std::string>();

    if (model == "CBR") {

    } else {
      std::ifstream input(model, std::ios::in);
      if (input.is_open()) {
        int ts;
        while (input >> ts) {
          std::cout << ts << " ";
          timer_config_.timestamps.push_back(ts);
        }
        input.close();
      } else {
        io_logger.error("Open model file failed!");
        exit(-1);
      }

      std::cout << std::endl;
    }
    burst_config_.burst_on = vm["burst-on"].as<unsigned>();
    burst_config_.burst_off = vm["burst-off"].as<unsigned>();
  }
}

void io_scheduler::create_burst_thread(std::function<void()> do_burst) {
  burst_threads_.emplace_back(std::move(do_burst));
}


void io_scheduler::io_loop() {
  system_clock::time_point prev_ts = system_clock::now(), cur_ts;
  size_t index = 0;

  if (precision_mode_) {
    while (!stop_) {
      uint64_t threash = timer_config_.timestamps[index];
      cur_ts = system_clock::now();
      auto diff_ts = std::chrono::duration_cast<nanoseconds>(cur_ts - prev_ts).count();

      if (unlikely(static_cast<uint64_t>(diff_ts) >= threash)) {
        int ret = io_queues_[timer_config_.queue_id]->send_packets();
        if (likely(ret > 0)) {
        }
        prev_ts = cur_ts;
        /*
        index++;
        if (unlikely(index > timer_config_.timestamps.size())) {
          index = 0;
        }
        */
      }
    }

  } else if (burst_mode_) {
    io_logger.info("burst mode activated!");
    if (smp::count > 1) {
      for (unsigned i = 0; i < nr_queue_; i++) {
        create_burst_thread([this, i] {
          pin_this_thread(i);
          while (!stop_) {
            burst_loop(i);
          }
        });
      }
    } else {
      create_burst_thread([this] {
        pin_this_thread(0);
        while(!stop_) {
          burst_loop(0);
        }
      });
    }
    for (auto&& t: burst_threads_) {
      t.join();
    }
  }
}

void io_scheduler::burst_loop(unsigned cpu_id) {
  std::this_thread::sleep_for(std::chrono::milliseconds(burst_config_.burst_off));
  io_logger.trace("Cpu {}: burst started!", cpu_id);
  system_clock::time_point start_ts = system_clock::now(), cur_ts;
  bool burst_stop = false;
  int cnt = 0;
  while (!burst_stop) {
    cur_ts = system_clock::now();
    auto diff_ts =
        std::chrono::duration_cast<milliseconds>(cur_ts - start_ts).count();
    if (unlikely(static_cast<uint64_t>(diff_ts) >= burst_config_.burst_on)) {
      burst_stop = true;
      io_logger.trace("cpu {}: burst stopped, burst {} packets!", cpu_id, cnt);
      cnt = 0;
    }
    unsigned num = cpu_id == 0 ? 0 : cpu_id - 1;
    auto ret = io_queues_[num]->burst_packets();
    if (ret <= 0) {
      //burst_stop = true;
      //io_logger.info("burst stopped, no available packets");
    } else {
      io_logger.trace("cpu {}: burst {} packets", cpu_id, ret);
      cnt += ret;
    }
  }
}

void io_scheduler::run() {
  // wait for the engine to start
  unsigned engine_ready = smp::count > 1 ? smp::count-1 : 1;
  while (nr_queue_ < engine_ready);
  io_logger.info("I/O thread started on core {}", cpu_id_);
  io_loop();
  io_logger.info("I/O thread stopped");

}

void io_scheduler::stop() {
  stop_ = true;
}

} // namespace infgen
