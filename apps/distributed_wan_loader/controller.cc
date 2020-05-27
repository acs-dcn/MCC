#include "proto/wan.pb.h"
#include "application.h"
#include "reactor.h"
#include "tcp_server.h"
#include "log.h"

using namespace infgen;
using namespace std::chrono_literals;
using namespace std::chrono;
namespace bpo = boost::program_options;

int main(int argc, char* argv[]) {
  application app;
  app.add_options()
    ("connections,c", bpo::value<unsigned>()->default_value(1),
     "number of concurrent connections")
    ("duration,d", bpo::value<unsigned>()->default_value(1),
     "test duration")
    ("length-mode,g", bpo::value<unsigned>()->default_value(1),
     "1 for fixed payload size, 2 for random size.")
    ("length,l", bpo::value<unsigned>()->default_value(16),
     "Length of payload for fixed-size mode (> 8)")
    ("idt-mode,j", bpo::value<unsigned>()->default_value(1),
     "1 for fixed IDT, 2 for random IDT")
    ("lambda,a", bpo::value<unsigned>()->default_value(32),
     "Parameter for Poisson distribution")
    ("interact-times,i", bpo::value<unsigned>()->default_value(1),
     "interaction times for each connection")
    ("think-time,t", bpo::value<int>()->default_value(100000),
     "think time between requests (ns)")
    ("port,p", bpo::value<unsigned>()->default_value(2222),
     "server bind port")
    ("workers,n", bpo::value<unsigned>()->default_value(1),
     "number of workers");
  app.run(argc, argv, [&]() {
    auto config = app.configuration();
    auto conn = config["connections"].as<unsigned>();
    auto duration = config["duration"].as<unsigned>();
    auto len_mode = config["length-mode"].as<unsigned>();
    auto idt_mode = config["idt-mode"].as<unsigned>();
    auto length = config["length"].as<unsigned>();
    auto lambda = config["lambda"].as<unsigned>();
    auto nr_interact = config["interact-times"].as<unsigned>();
		auto think_time = config["think-time"].as<int>();
    auto port = config["port"].as<unsigned>();
    auto workers = config["workers"].as<unsigned>();

    unsigned workers_online = 0;
    unsigned workers_finished = 0;

    pin_this_thread(10);

    ipv4_addr addr("0.0.0.0", port);
    std::vector<connptr> worker_conns(workers, nullptr);
    std::vector<int> worker_desc(workers, 0);
    std::vector<int> rx_bytes(workers, 0);
    std::vector<int> completes(workers, 0);
    std::vector<int> delay(workers, 0);

    uint64_t total_reqs = 0;

    std::array<std::string, 2> status = {"OFF", "ON"};

    auto svr = tcp_server::create_tcp_server(make_ipv4_address(addr));
    if (svr == nullptr) {
      app_logger.error("Create server failed");
      exit(-1);
    }

    svr->when_disconnect([&] (const connptr& con) mutable {
      app_logger.info("client {} offline", con->get_id());
      worker_desc[con->get_id()] = 0;
      workers_online--;
    });

    svr->on_message([&] (const connptr& con, std::string msg) mutable {
      report r;
      con->get_input().consume(msg.size());
      if (!r.ParseFromString(msg)) {
        app_logger.error("failed to parse message!");
      }

      if (r.has_note()) {
      // the first packet is for status report
        auto client_id = r.client_id();
        con->set_id(client_id);
        app_logger.info("client {} online", client_id);
        worker_desc[client_id] = 1;
        worker_conns[client_id] = con;
        workers_online++;

        if (workers_online >= workers) {
          // start distributing workload
          app_logger.info("workers ready, distributing workloads");
          command cmd;
          cmd.set_conn(conn);
          cmd.set_duration(duration);
          cmd.set_length_mode(len_mode);
          cmd.set_length(length);
          cmd.set_idt_mode(idt_mode);
          cmd.set_lambda(lambda);
          cmd.set_interact_times(nr_interact);
					cmd.set_think_time(think_time);
          // set the start timer to 3s later
          auto tp = system_clock::now();
          app_logger.trace("now: {}", 
              duration_cast<milliseconds>(tp.time_since_epoch()).count());
          tp = tp + 3000ms;
          auto start_ts = duration_cast<milliseconds>(tp.time_since_epoch()).count();
          app_logger.trace("start ts: {}", start_ts);
          cmd.set_start_ts(start_ts);
          std::string packet;
          cmd.SerializeToString(&packet);

          for (auto& c: worker_conns) {
            if (c) {
              c->send_packet(packet);
            }
          }
        }
      } else {
        // do the stat report
        auto client_id = r.client_id();
        completes[client_id] = r.completes();

        fmt::print("\nstatistics from client {}:\n", client_id);
        fmt::print("requests: {}\n", r.completes());

        total_reqs += r.completes();

        workers_finished++;

        if (workers_finished >= workers) {
          app_logger.info("All workers done!");
          fmt::print("\n\n===============summary============================\n");
          fmt::print("{} requests in {} s\n", total_reqs, duration);
          fmt::print("request/sec: {}\n", static_cast<double>(total_reqs) / duration);
          fmt::print("==================================================\n");
          engine().stop();
        }

      }
    });
    engine().run();
  });
}
