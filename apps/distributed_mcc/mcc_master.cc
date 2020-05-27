#include "proto/mcc.pb.h"
#include "application.h"
#include "reactor.h"
#include "tcp_server.h"
#include "log.h"

using namespace infgen;
namespace bpo = boost::program_options;

int main(int argc, char* argv[]) {
  application app;
  app.add_options()
    ("epoch,e", bpo::value<float>()->default_value(1.0),
      "the repeat period(s)")
    ("connections,c", bpo::value<unsigned>()->default_value(1),
     "number of concurrent connections")
    ("setup-time,s", bpo::value<unsigned>()->default_value(1),
     "connection setup time(s)")
    ("wait-time,w", bpo::value<unsigned>()->default_value(1),
     "wait time after connection setup(s)")
    ("duration,d", bpo::value<unsigned>()->default_value(1),
     "test duration")
    ("burst,b", bpo::value<unsigned>()->default_value(1),
     "number of bursted packets at one time")
    ("port,p", bpo::value<unsigned>()->default_value(2222),
     "bind port")
    ("ratio,r", bpo::value<float>()->default_value(0.05),
     "ratio of request packets")
		("length,i", bpo::value<unsigned>()->default_value(16), 
		 "length of each request (>8)")
		("think-time,t", bpo::value<int>()->default_value(50000), 
		 "think time between requests (ns)")
    ("workers,n", bpo::value<unsigned>()->default_value(1),
     "number of workers");
  app.run(argc, argv, [&]() {
    auto config = app.configuration();
    auto epoch = static_cast<unsigned>(1000 * config["epoch"].as<float>()); // Milliseconds
    auto conn = config["connections"].as<unsigned>();
    auto setup_time = config["setup-time"].as<unsigned>();
    auto wait_time = config["wait-time"].as<unsigned>();
    auto duration = config["duration"].as<unsigned>();
    auto burst = config["burst"].as<unsigned>();
    auto port = config["port"].as<unsigned>();
    auto ratio = config["ratio"].as<float>();
		auto length = config["length"].as<unsigned>();
		auto think_time = config["think-time"].as<int>();
    auto workers = config["workers"].as<unsigned>();

    unsigned workers_online = 0;

    pin_this_thread(10);

    ipv4_addr addr("0.0.0.0", port);
    std::vector<connptr> worker_conns(workers, nullptr);
    std::vector<int> worker_desc(workers, 0);
    std::vector<int> connected(workers, 0);
    std::vector<int> tx_packets(workers, 0);
    std::vector<int> rx_packets(workers, 0);

    std::array<std::string, 2> status = {"OFF", "ON"};

    auto svr = tcp_server::create_tcp_server(make_ipv4_address(addr));
    if (svr == nullptr) {
      app_logger.error("create server failed");
      exit(-1);
    }

    svr->when_disconnect([&] (const connptr& con) mutable {
      app_logger.info("client {} offline", con->get_id());
      connected[con->get_id()] = 0;
			tx_packets[con->get_id()] = 0;
			rx_packets[con->get_id()] = 0;

      worker_desc[con->get_id()] = 0;
      
			workers_online--;
    });

    engine().add_periodic_task_at<infinite>(system_clock::now(), 1s, [&] {
      system("clear");
      std::time_t t = std::time(nullptr);
      fmt::print("{:%Y-%m-%d %T}\n", fmt::localtime(t));
      fmt::print("\n");
      fmt::print("Node\tStatus\tConnected\tTx\t\tRx\t\n");
      for (unsigned i = 0; i < workers; i++) {
        fmt::print("{}\t{}\t{}\t\t{}\t\t{}\n", i, status[worker_desc[i]],
            connected[i], tx_packets[i], rx_packets[i]);
        //tx_packets[i] = 0;
        //rx_packets[i] = 0;
      }
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
          app_logger.info("all workers ready, distributing workloads");
          command cmd;
          cmd.set_conn(conn);
          cmd.set_burst(burst);
          cmd.set_epoch(epoch);
          cmd.set_setup_time(setup_time);
          cmd.set_wait_time(wait_time);
          cmd.set_duration(duration);
					cmd.set_length(length);
					cmd.set_think_time(think_time);
          cmd.set_ratio(ratio);
          auto tp = system_clock::now() + 3000ms;
          auto start_ts = duration_cast<milliseconds>(tp.time_since_epoch()).count();
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
        connected[client_id] = r.connected();
        tx_packets[client_id] = r.tx_packets();
        rx_packets[client_id] = r.rx_packets();
      }
    });
    engine().run();
  });
}
