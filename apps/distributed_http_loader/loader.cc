#include "application.h"
#include "connection.h"
#include "log.h"
#include "reactor.h"
#include "smp.h"
#include "distributor.h"
#include "http/http_parser.h"
#include "proto/http.pb.h"

#include <chrono>
#include <memory>
#include <vector>

using namespace infgen;
using namespace std::chrono;
using namespace std::chrono_literals;
namespace bpo = boost::program_options;

class http_client {
private:
  unsigned duration_;
  unsigned conn_per_core_;
  uint64_t conn_finished_{0};
	int think_time_; /// ns

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
    void do_req() {
      flow->send_packet("GET / HTTP/1.1\r\n"
          "HOST: 192.168.2.2\r\n\r\n");
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
    fmt::print("request on cpu {}: {}\n", engine().cpu_id(), stats.done_reqs);
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

      conn->on_message([this, http_conn](const connptr& conn, std::string& msg) {
        conn->get_input().consume(msg.size());
        http_conn->complete_request();

				//@ processing time (ns)
        unsigned val = Fibonacci_service(think_time_); 
				if (msg.size()) {
					msg[0] = static_cast<int>(val % 127);
				}
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

      conn->when_ready([http_conn] (const connptr& conn){
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
      engine().add_oneshot_task_after(seconds(duration_), [this] {
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
    ("server-ip,s", bpo::value<std::string>(), "server ip address")
    ("server-port,p", bpo::value<unsigned>()->default_value(2222), "server port")
    ("local-ip,l", bpo::value<std::string>(), "local ip address")
    ("client-id,n", bpo::value<unsigned>(), "client id");

  app.run(argc, argv, [&app] {
    auto &config = app.configuration();
    auto ip = config["server-ip"].as<std::string>();
    auto local_ip = config["local-ip"].as<std::string>();
    auto port = config["server-port"].as<unsigned>();
    auto id = config["client-id"].as<unsigned>();
    auto dest = config["dest"].as<std::string>();

    ipv4_addr server_addr(ip, port);
    ipv4_addr local_addr(local_ip);

    uint64_t conns, duration;
		int think_time;
    int64_t start_ts;
    system_clock::time_point start_tp;

    system_clock::time_point started, finished;
    adder reqs, bytes, total_delay;

    auto clients = new distributor<http_client>;

    auto conn = engine().connect(make_ipv4_address(server_addr), make_ipv4_address(local_addr));
    conn->when_ready([&](const connptr& con) {
      app_logger.info("server connected!");
      report r;
      r.set_client_id(id);
      report::notice *n = r.mutable_note();
      n->set_online(true);
      std::string packet;
      r.SerializeToString(&packet);
      conn->send_packet(packet);
    });

    conn->on_message([&](const connptr& conn, std::string msg) mutable {
      command cmd;
      conn->get_input().consume(msg.size());
      if (!cmd.ParseFromString(msg)) {
        app_logger.error("Failed to parse message!");
      }

      conns = cmd.conn();
      duration = cmd.duration();
			think_time = cmd.think_time();
      start_ts = cmd.start_ts();

      start_tp = start_tp + milliseconds(start_ts);


      fmt::print(
          "configuration: \n\tconnections: {}\n\tduration: {}\n\tthreads:{}\n",
          conns, duration, smp::count-1);
      if (conns % (smp::count-1) != 0) {
        fmt::print("error: conn needs to be n * cpu_nr \n");
        exit(-1);
      }

      engine().add_oneshot_task_at(start_tp, [&, conn]() mutable {
        clients->start(duration, conns, think_time);

        started = system_clock::now();
        fmt::print("connections: {}\n", conns);

        clients->invoke_on_all(&http_client::connect, ipv4_addr(dest, 80));
        clients->invoke_on_all(&http_client::run);

        finished = started;

        clients->when_done([&, clients, conn]() mutable {
          app_logger.info("load test finished, running stats collect process...");
          finished = system_clock::now();
          clients->map_reduce(reqs, &http_client::total_reqs);
          clients->map_reduce(bytes, &http_client::rx_bytes);
          clients->map_reduce(total_delay, &http_client::acc_delay);

          engine().add_oneshot_task_after(1s, [&, clients, conn] {
            auto total_reqs = reqs.result();
            auto rx_bytes = bytes.result();
            auto avg_delay = total_delay.result() / total_reqs;
            auto elapsed = duration_cast<seconds>(finished - started);
            auto secs = elapsed.count();
            fmt::print("total cpus: {}\n", smp::count-1);
            fmt::print("=============== summary =========================\n");
            fmt::print("{} requests in {}s, {}MB read\n", total_reqs, secs, rx_bytes / 1024 / 1024);
            fmt::print("request/sec:  {}\n", static_cast<double>(total_reqs) / secs);
            fmt::print("transfer/sec: {}MB\n", static_cast<double>(rx_bytes) / 1024 / 1024 / secs);
            fmt::print("average delay: {}us\n", avg_delay);
            fmt::print("=============== done ============================\n");

            report r;
            r.set_client_id(id);
            r.set_completes(total_reqs);
            r.set_delay(avg_delay);
            r.set_rx_bytes(rx_bytes);
            std::string packet;
            r.SerializeToString(&packet);
            conn->send_packet(packet);

            engine().add_oneshot_task_after(1s, [&, clients, conn] {
              //clients->stop();
              engine().stop();
            });
          });
        });

      });

    });
    engine().run();


    delete clients;
  });
  return 0;
}
