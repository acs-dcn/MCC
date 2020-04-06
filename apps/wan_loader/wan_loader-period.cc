#include "application.h"
#include "connection.h"
#include "log.h"
#include "reactor.h"
#include "smp.h"
#include "distributor.h"
#include "http/http_parser.h"

#include <chrono>
#include <memory>
#include <vector>

using namespace infgen;
namespace bpo = boost::program_options;

class http_client {
private:
	unsigned conns_
  unsigned duration_;
  unsigned conn_per_core_;
  uint64_t conn_finished_{0};

	unsigned setup_time_;
	unsigned wait_time_;
	unsigned req_length_;	
	unsigned nr_interact_t_;

  ipv4_addr server_addr_;
  distributor<http_client>* container_;

  struct metrics {
    uint64_t acc_delay;
    uint64_t avg_delay;
    uint64_t done_reqs;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
  } stats;

  struct http_connection {
    http_connection(connptr c): flow(c) {}
    connptr flow;
    uint64_t nr_done {0};
    system_clock::time_point send_ts;
    system_clock::time_point recv_ts;
    uint64_t acc_delay {0};

    unsigned header_len;
    unsigned file_len;
    bool header_set;
		
		unsigned nr_interact_;      /// times of interaction per connection, default 1
		std::string request_;

    void do_req() {
      //flow->send_packet("GET / HTTP/1.1\r\n"
      //    "HOST: 192.168.2.2\r\n\r\n");
      flow->send_packet(request_);
      send_ts = system_clock::now();
    }

    void complete_request() {
      nr_done++;
      recv_ts = system_clock::now();
      auto delay = duration_cast<microseconds>(recv_ts - send_ts).count();
      acc_delay += delay;
    }

    void finish() {
      flow->when_closed([this] {
        app_logger.trace("Test complete, connection {} closed", flow->get_id());
      });
      flow->close();
    }
  };

public:
  http_client(unsigned conns, unsigned duration, unsigned concurrency, unsigned setup_time, 
							unsigned wait_time, unsigned req_length, unsigned nr_interact)
      : conns_(conns), duration_(duration), conn_per_core_(concurrency / (smp::count-1)), 
			setup_time_(setup_time), wait_time_(wait_time), 
			req_length_(req_length), nr_interact_t_(nr_interact){
    stats.acc_delay = 0;
    stats.avg_delay = 0;
    stats.done_reqs = 0;
  }

private:
  std::vector<std::shared_ptr<http_connection>> conns_;
public:
  // methods for map-reduce
  uint64_t total_reqs() {
    fmt::print("Request on cpu {}: {}\n", engine().cpu_id(), stats.done_reqs);
    return stats.done_reqs;
  }
  uint64_t tx_bytes() { return stats.tx_bytes; }
  uint64_t rx_bytes() { return stats.rx_bytes; }
  uint64_t acc_delay() { return stats.acc_delay; }

  void running(ipv4_addr server_addr) {
    server_addr_ = server_addr;
		auto conns_block = conns_ / setup_time_;
		for (unsigned i = 0; i < setup_time_; i++) {
			engine.add_oneshot_task_after(i * 1s, [=] {
				for (unsigned j = 0; j < conns_block; ++j)  {		
					auto conn = engine().connect(make_ipv4_address(server_addr));
					auto http_conn = std::make_shared<http_connection>(conn);
					
					http_conn->nr_interact_ = nr_interact_t_;
					
					http_conn->request_ = std::string(req_length_, '0');
					http_conn->request_[5] = 0x01;
					http_conn->request_[6] = 0x02;
					http_conn->request_[7] = 0x01;
					
					conns_.push_back(http_conn);

					conn->when_recved([this, http_conn, conn] (const connptr& conn) {
						if (!http_conn->header_set) {
							// parse header
							std::string resp_str = conn->get_input().string();
							//conn->get_input().consume(resp_str.size());
							//fmt::print("response: {}\n", resp_str);
							http_conn->header_set = true;
						} else {
							std::string content = conn->get_input().string();
							//conn->get_input().consume(content.size());
							//fmt::print("content: {}\n", content);
						}
					});

					conn->on_message([http_conn, this](const connptr& conn, std::string& msg) {
						conn->get_input().consume(msg.size());
						http_conn->complete_request();
						
						if (http_conn->nr_interact_ > 0 && 
								conn->get_state() == tcp_connection::state::connected) { // More interactions
							http_conn->nr_interact_--;
							http_conn->do_req();  // Send another request
						} else { // Finished interacting
							conn->close();

							//conn->reconnect();    // Supply more client
							//http_conn->nr_interact_ = nr_interact_t_;
							engine().add_oneshot_task_after(1s, [conn, http_conn] {
								conn->reconnect(); // Supply more client 
								http_conn->nr_interact_ = nr_interact_t_;
								});		
						}
					});

					conn->when_close([this] (const connptr& conn) {
						// conn->reconnect();
						// engine().add_oneshot_task_after(3s, [conn] {conn->reconnect(); });		
					});

					conn->when_disconnect([this] (const connptr& conn) {
						//conn->reconnect();
						engine().add_oneshot_task_after(1s, [conn] {conn->reconnect(); });		
					});

					conn->when_ready([http_conn, this] (const connptr& conn){
						//http_conn->do_req();
					});
					
				}
			});
		}

		//Warm-up and Benchmarking
	  engine().add_oneshot_task_after(seconds(wait_time_ + setup_time_),
                                              [this] { do_req(); });

    engine().add_oneshot_task_after(seconds(duration_), [this] {
      for (auto c: conns_) {
        c->close();
      }
      engine().stop();
    });	
  }

  void finish() {
    app_logger.info("loader {} finished", engine().cpu_id());
    container_->end_game(this);
  }

  void set_container(distributor<http_client>* container) {
    container_ = container;
  }

  void stat_run() {
    if (duration_ > 0) {
      engine().add_oneshot_task_after(std::chrono::seconds(duration_), [this] {
        for (auto&& http_conn: conns_) {
          http_conn->finish();
          stats.done_reqs += http_conn->nr_done;
          stats.acc_delay += http_conn->acc_delay;
          stats.tx_bytes += http_conn->flow->tx_bytes();
          stats.rx_bytes += http_conn->flow->rx_bytes();
        }
        finish();
      });
    }
  }

  void stop() {
    engine().stop();
  }
};

int main(int argc, char **argv) {
  application app;
  app.add_options()
		("length,l", bpo::value<unsigned>()->default_value(16), "length of message (> 8)")
    ("conn,c", bpo::value<unsigned>()->default_value(100), "total connections")
    ("interact-number,i", bpo::value<unsigned>()->default_value(0), "times of interactions per connection")
    ("setup-time,s", bpo::value<unsigned>()->default_value(1), "connection setup time(s)")
    ("wait-time,w", bpo::value<unsigned>()->default_value(1), "wait time before sending requests(s)")
    ("duration,d", bpo::value<unsigned>()->default_value(0), "duration of test in seconds");
  app.run(argc, argv, [&app] {
    auto &config = app.configuration();
    auto server = config["dest"].as<std::string>();
    auto total_conn = config["conn"].as<unsigned>();
    auto nr_interact = config["interact-number"].as<unsigned>();
    auto setup = config["setup-time"].as<unsigned>();
    auto wait = config["wait-time"].as<unsigned>();
    auto duration = config["duration"].as<unsigned>();
		auto length = config["length"].as<unsigned>();

    if (total_conn % (smp::count-1) != 0) {
      fmt::print("Error: conn needs to be n * cpu_nr \n");
      exit(-1);
    }

    auto clients = new distributor<http_client>;
    clients->start(total_conn, duration, total_conn, setup, wait, length, nr_interact);

    //auto started = system_clock::now();
    system_clock::time_point started;
    fmt::print("Running {}s test @ server: {}\n", duration, server);
    fmt::print("connections: {}\n", total_conn);

    clients->invoke_on_all(&http_client::running, ipv4_addr(server, 80));

    


		adder reqs, bytes, total_delay;
    system_clock::time_point finished = started;

    clients->when_done([&finished, clients, &reqs, &bytes, &total_delay]() mutable {
      app_logger.info("load test finished, running stats collect process...");
      finished = system_clock::now();
      clients->map_reduce(reqs, &http_client::total_reqs);
      clients->map_reduce(bytes, &http_client::rx_bytes);
      clients->map_reduce(total_delay, &http_client::acc_delay);

      engine().add_oneshot_task_after(1s, [clients] {
        clients->stop();
        engine().stop();
      });


    });

    engine().run();

    auto total_reqs = reqs.result();
    auto data_recved = bytes.result();
    auto avg_delay = total_delay.result() / total_reqs;
    std::chrono::duration<double> elapsed = finished - started;
    auto secs = elapsed.count();

    fmt::print("total cpus: {}\n", smp::count);
    fmt::print("=============== summary =========================\n");
    fmt::print("{} requests in {}s, {}MB read\n", total_reqs, secs, data_recved / 1024 / 1024);
    fmt::print("Request/sec:  {}\n", static_cast<double>(total_reqs) / secs);
    fmt::print("Transfer/sec: {}MB\n", static_cast<double>(data_recved / 1024 / 1024) / secs);
    fmt::print("Average delay: {}us\n", avg_delay);
    fmt::print("=============== done ============================\n");

    delete clients;

  });
  return 0;
}
