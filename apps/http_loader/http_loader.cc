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

std::string request;
unsigned req_length;


class http_client {
private:
  unsigned duration_;
  unsigned conn_per_core_;
  uint64_t conn_finished_{0};
	int think_time_;

  ipv4_addr server_addr_;
  distributor<http_client>* container_;

  struct metrics {
    uint64_t acc_delay;
    uint64_t avg_delay;
    uint64_t done_reqs;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
  } stats;

  unsigned Fibonacci_service(int delay) {
    unsigned pre = 0;
    unsigned cur = 1;
    int loops = delay * 0.75;

    while (loops-- > 0) {
      cur += pre;
      pre = cur - pre;
    }   

    return pre;
  }

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

    bool request_sent;
		std::string request_;
    void do_req() {
      //flow->send_packet("GET / HTTP/1.1\r\n"
      //    "HOST: 192.168.2.2\r\n\r\n");
      flow->send_packet(/*request*/request_);
			request_sent = true;
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
  http_client(unsigned duration, unsigned concurrency, int think_time)
      : duration_(duration), conn_per_core_(concurrency / (smp::count-1)),
				think_time_(think_time){
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

  void connect(ipv4_addr server_addr) {
    server_addr_ = server_addr;
    for (unsigned i = 0; i < conn_per_core_; i++) {
      auto conn = engine().connect(make_ipv4_address(server_addr));
      auto http_conn = std::make_shared<http_connection>(conn);
			http_conn->request_ = std::string(req_length, '0');
			http_conn->request_[5] = 0x01;
			http_conn->request_[6] = 0x02;
			http_conn->request_[7] = 0x01;
      
			conns_.push_back(http_conn);

      conn->when_recved([this, http_conn] (const connptr& conn) {
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

				// processing time (ns)
				unsigned val = Fibonacci_service(think_time_);
        if (msg.size()) {
          msg[0] = static_cast<int>(val % 127);
        }

        http_conn->complete_request();
        if (conn->get_state() == tcp_connection::state::connected) {
          http_conn->do_req();
        }
          //conn->close();
      });

      conn->when_closed([conn] {
        conn->reconnect();
      });

      conn->when_disconnect([this] (const connptr& conn) {
        conn->reconnect();
      });

      conn->when_ready([http_conn, this] (const connptr& conn){
        http_conn->do_req();
      });
    }
  }

  void finish() {
    app_logger.info("loader {} finished", engine().cpu_id());
    container_->end_game(this);
  }

  void set_container(distributor<http_client>* container) {
    container_ = container;
  }

  void run() {
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
		("think-time,t", bpo::value<int>()->default_value(0), "think time between requests (ns)")
    ("duration,d", bpo::value<unsigned>()->default_value(0), "duration of test in seconds");
  app.run(argc, argv, [&app] {
    auto &config = app.configuration();
    auto server = config["dest"].as<std::string>();
    auto total_conn = config["conn"].as<unsigned>();
    auto duration = config["duration"].as<unsigned>();
		auto think_time = config["think-time"].as<int>();
		auto length = config["length"].as<unsigned>();

    if (total_conn % (smp::count-1) != 0) {
      fmt::print("Error: conn needs to be n * cpu_nr \n");
      exit(-1);
    }
	/* Payload with fixed length */
		req_length = length;
		request = std::string(length,'0'); //16, '0'
    request[5] = 0x01;
    request[6] = 0x02;
    request[7] = 0x01;

    auto clients = new distributor<http_client>;
    clients->start(duration, total_conn, think_time);

    //auto started = system_clock::now();
    system_clock::time_point started;
    fmt::print("Running {}s test @ server: {}\n", duration, server);
    fmt::print("connections: {}\n", total_conn);

    clients->invoke_on_all(&http_client::connect, ipv4_addr(server, 80));

    engine().add_oneshot_task_after(3s, [clients,&started]() {
      clients->invoke_on_all(&http_client::run);
      started = system_clock::now();
    });

    clients->invoke_on_all(&http_client::run);


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
