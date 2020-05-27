#include "proto/http.pb.h"
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
		("think-time,t", bpo::value<int>()->default_value(50000),
		 "think time between requests (ns)")
    ("port,p", bpo::value<unsigned>()->default_value(2222),
     "server bind port")
    ("workers,n", bpo::value<unsigned>()->default_value(1),
     "number of workers");
  app.run(argc, argv, [&]() {
    auto config = app.configuration();
    auto conn = config["connections"].as<unsigned>();
    auto duration = config["duration"].as<unsigned>();
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

    uint64_t total_rx = 0, total_reqs = 0, avg_delay = 0;

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
        rx_bytes[client_id] = r.rx_bytes();
        delay[client_id] = r.delay();

        fmt::print("\nstats from client {}:\n", client_id);
        fmt::print("requests: {}\n", r.completes());
        fmt::print("delay: {} us\n", r.delay());
        fmt::print("rx: {} MB\n\n", static_cast<double>(r.rx_bytes()) / 1024 / 1024);

        total_reqs += r.completes();
        avg_delay += r.delay();
        total_rx += r.rx_bytes();

        workers_finished++;

        if (workers_finished >= workers) {
          app_logger.info("workers done!");
          fmt::print("===============summary============================\n");
          fmt::print("{} requests in {} s, {} MB read\n", 
              total_reqs, duration, static_cast<double>(total_rx) / 1024 / 1024);
          fmt::print("request/sec: {}\n", static_cast<double>(total_reqs) / duration);
          fmt::print("transfer/sec: {} MB\n", static_cast<double>(total_rx) / 1024 / 1024 / duration);
          fmt::print("average delay: {} us\n", static_cast<double>(avg_delay) / workers);
          fmt::print("==================================================\n");
          engine().stop();
        }

      }
    });
    engine().run();
  });
}
