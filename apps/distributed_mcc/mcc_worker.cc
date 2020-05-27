#include "application.h"
#include "connection.h"
#include "reactor.h"
#include "log.h"
#include "proto/mcc.pb.h"

#include "smp.h"
#include "distributor.h"

#include <vector>
#include <random>

using namespace infgen;
using namespace std;

namespace bpo = boost::program_options;
logger client_logger("client_log", true);

class client {
private:
  unsigned nr_conns_;
  unsigned epoch_;
  unsigned burst_;
  unsigned setup_time_;
  double request_ratio_;
  unsigned wait_time_;

  std::vector<connptr> conns_;
  std::vector<int> ref_;

	//@wuwenqing, for fixed length of payload
	unsigned req_length_;
	int think_time_; //ns
	system_clock::time_point start_tp_;

  distributor<client>* container_;

  struct metrics {
    unsigned connected;
    unsigned retry;
    unsigned send;
    unsigned request;
    unsigned received;
  };

  metrics stats_sec, stats_log;

  std::string heartbeat_;
  std::string request_;
  unsigned duration_;

  void clear_stats(metrics& stats) {
    stats.send = 0;
    stats.request = 0;
    stats.received = 0;
  }

  void send_request(unsigned j) {
    conns_[j]->send_packet(request_);
    stats_sec.request++;
    stats_sec.send++;
    stats_log.request++;
    stats_log.send++;
  }

  void send_heartbeat(unsigned j) {
    conns_[j]->send_packet(heartbeat_);
    stats_sec.send++;
    stats_log.send++;
  }

  unsigned Fibonacci_service(int delay) { /// ns
    unsigned pre = 0;
    unsigned cur = 1;
		int loops = delay * 0.75;

  	while (loops-- > 0) {
    	cur += pre;
    	pre = cur - pre;
  	}

  	return pre;
  }

public:
  client(unsigned conns, unsigned epoch, unsigned burst, unsigned setup_time, unsigned wait_time,
         unsigned duration, double ratio, unsigned length, int think_time, system_clock::time_point start_tp)
      : nr_conns_(conns), epoch_(epoch), burst_(burst), setup_time_(setup_time),
        request_ratio_(ratio), wait_time_(wait_time), req_length_(length), think_time_(think_time), start_tp_(start_tp), stats_sec(metrics{}), stats_log(metrics{}),
        heartbeat_(length, 0), request_(length, 0), duration_(duration) {
      request_[5] = 0x01;
      request_[6] = 0x02;

			heartbeat_[5] = 0x00;
			heartbeat_[6] = 0x02;
      heartbeat_[8] = 0x08;
      app_logger.info("client created");
  }

  void set_container(distributor<client>* container) {
    container_ = container;
  }

  uint64_t connected_sec() { return stats_sec.connected; }
  uint64_t send_sec() { return stats_sec.send; }
  uint64_t request_sec() { return stats_sec.request; }
  uint64_t received_sec() { return stats_sec.received; }
  uint64_t retry_sec() { return stats_sec.retry; }

  uint64_t connected_log() { return stats_log.connected; }
  uint64_t send_log() { return stats_log.send; }
  uint64_t request_log() { return stats_log.request; }
  uint64_t received_log() { return stats_log.received; }

  void flush_log_stats() {
    stats_log.send = 0;
    stats_log.request = 0;
    stats_log.received = 0;
  }
  void stop() {
    engine().stop();
  }

  void start(ipv4_addr server_addr) {
    ref_.resize(burst_);
    std::iota(ref_.begin(), ref_.end(), 0);
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(ref_.begin(), ref_.end(), g);

    auto block = nr_conns_ / setup_time_;
    app_logger.info("start loading...");
    for (unsigned i = 0; i < setup_time_; i++) {
      engine().add_oneshot_task_after(i * 1s, [=] {
        for (unsigned j = 0; j < block; j++) {
          auto conn = engine().connect(make_ipv4_address(server_addr));
          conn->when_ready([this] (const connptr& conn) {
            stats_log.connected++;
            stats_sec.connected++;
            conns_.push_back(conn);
            if (conns_.size() >= nr_conns_) {
              app_logger.trace("all connections ready!");
            }
          });
          conn->when_recved([this] (const connptr& conn) {
            stats_sec.received++;
            stats_log.received++;
            std::string s = conn->get_input().string();
            conn->get_input().consume(s.size());
      			// "Think time"
						unsigned val = Fibonacci_service(think_time_); 
            request_[9] = static_cast<int>(val % 127);
          });

          conn->when_closed([this] {
            stats_sec.connected--;
            stats_log.connected--;
          });

          conn->when_failed([this] (const connptr& conn) {
						// @ wuwenqing
            stats_sec.connected--;
            stats_sec.retry++;

            stats_log.connected--;
            stats_log.retry++;
            conn->reconnect();
          });

          conn->when_disconnect([this] (const connptr& conn) {
            stats_sec.connected--;
            stats_sec.retry++;

            stats_log.connected--;
            stats_log.retry++;
            conn->reconnect();
            //auto newconn = conn->reconnect();
            //conns_.push_back(newconn);
          });
        }
      });
    }

    engine().add_oneshot_task_after(seconds(wait_time_ + setup_time_),
    //engine().add_oneshot_task_at(start_tp_,
                                    [this] { do_req(); });

    engine().add_oneshot_task_after(seconds(duration_), [this] {
      for (auto c: conns_) {
        c->close();
      }
      engine().stop();
    });
  }

  void print_stats() {
    fmt::print("[engine {}]\tconnected: {} \tretry: {}\tsend: {}\t"
                 "request: {}\treceived: {}\n", engine().cpu_id(),
                 stats_sec.connected, stats_sec.retry, stats_sec.send, stats_sec.request,
                 stats_sec.received);
    clear_stats(stats_sec);
  }

  void do_req() {
    auto blocks = conns_.size() / burst_;
    auto interval = epoch_ / blocks;

    for (unsigned i = 0; i < blocks; i++) {
      engine().add_periodic_task_at<infinite>(
        system_clock::now() + i * milliseconds(interval), milliseconds(epoch_), [=] {
          for (unsigned j = i * burst_;
               j < (i + 1) * burst_ && j < conns_.size(); j++) {
            if (conns_[j]->get_state() == tcp_connection::state::connected) {
              if (ref_[j % burst_] < static_cast<int>(burst_ * request_ratio_)) {
                send_request(j);
              } else {
                send_heartbeat(j);
              }
            }
          }
      });
    }
  }
};

int main(int argc, char **argv) {
  application app;
  app.add_options()
    ("server-ip,s", bpo::value<std::string>(), "server ip address")
    ("server-port,p", bpo::value<unsigned>()->default_value(2222), "server port")
    ("local-ip,l", bpo::value<std::string>(), "local ip address")
    ("client-id,n", bpo::value<unsigned>(), "client id")
    ("log-duration", bpo::value<unsigned>()->default_value(10), "duration betwwen logs")
    ("verbose,v", bpo::value<unsigned>()->default_value(0), "show verbose message");

  app.run(argc, argv, [&app] {
    auto config = app.configuration();
    auto ip = config["server-ip"].as<std::string>();
    auto local_ip = config["local-ip"].as<std::string>();
    auto port = config["server-port"].as<unsigned>();
    auto id = config["client-id"].as<unsigned>();
    auto log_duration = config["log-duration"].as<unsigned>();
    auto dest = config["dest"].as<std::string>();
    auto verbose = config["verbose"].as<unsigned>();
    ipv4_addr addr(ip, port);
    ipv4_addr local(local_ip);

    uint64_t conns, burst, epoch, start_ts, wait, duration, setup, length;
		int think_time;
    double ratio;
    system_clock::time_point start_tp;

    adder connected, send, request, received, retry;
    adder send_log, request_log, received_log, connected_log;
    auto loaders = new distributor<client>;

    auto conn = engine().connect(make_ipv4_address(addr), make_ipv4_address(local));
	  app_logger.info("Worker Connecting.\n\n");
    conn->when_ready([&](const connptr& con) {
      app_logger.info("server connected");
      report r;
      r.set_client_id(id);
      report::notice* n = r.mutable_note();
      n->set_online(true);
      std::string packet;
      r.SerializeToString(&packet);
      conn->send_packet(packet);
    });

    conn->when_recved([&](const connptr& con) mutable {
      command cmd;
      std::string s = conn->get_input().string();
      conn->get_input().consume(s.size());
      if (!cmd.ParseFromString(s)) {
        app_logger.error("failed to parse message");
      }

      conns = cmd.conn();
      burst = cmd.burst();
      epoch = cmd.epoch();
      setup = cmd.setup_time();
      wait = cmd.wait_time();
      duration = cmd.duration();
      ratio = cmd.ratio();
			length = cmd.length();
			think_time = cmd.think_time();
      start_ts = cmd.start_ts();

      start_tp += milliseconds(start_ts);

      if (verbose) {
        fmt::print(
            "configuration: \n\tconnections: {}\n\tepoch: {}s\n\t"
            "burst: {}\n" "\tthreads: {}\n\t",
            conns, static_cast<float>(epoch)/1000, burst, smp::count-1);
      }

		system_clock::time_point start_point;
    start_point += milliseconds(start_ts);
		start_point += seconds(setup);
		start_point += seconds(wait);
    engine().add_oneshot_task_at(start_tp, [&]() mutable {
      loaders->start(conns / (smp::count-1), epoch, burst / (smp::count-1),
          setup, wait, duration, ratio, length, think_time, start_point);
      loaders->invoke_on_all(&client::start, ipv4_addr(dest, 80));

      engine().add_periodic_task_at<infinite> (
          system_clock::now(), 1s, [&]() mutable {
            loaders->map_reduce(connected, &client::connected_sec);
            loaders->map_reduce(request, &client::request_sec);
            loaders->map_reduce(send, &client::send_sec);
            loaders->map_reduce(received, &client::received_sec);
            loaders->map_reduce(retry, &client::retry_sec);
            if (verbose) {
              loaders->invoke_on_all(&client::print_stats);
            }

            engine().add_oneshot_task_after(100ms, [&] () mutable {
              if (verbose) {
                fmt::print("[ALL]\t\tconnected: {} \tretry: {}\t"
                            "send: {}\trequest: {}\treceived: {}\n",
                           connected.result(), retry.result(), send.result(),
                           request.result(), received.result());
              }
              report r;
              r.set_client_id(id);
              r.set_connected(connected.result());
              r.set_tx_packets(send.result());
              r.set_rx_packets(received.result());
              std::string packet;
              r.SerializeToString(&packet);

							//@ wuwenqing
							fmt::print("Total Connected: {}\tsend: {}\treceived: {}\n", 
								connected.result(), send.result(), received.result());
              if (conn->get_state() == tcp_connection::state::connected) {
                conn->send_packet(packet);
              } else {
                engine().stop();
              }

              connected.reset();
              request.reset();
              send.reset();
              received.reset();
              retry.reset();
              fmt::print("\n");

            });
      });

      engine().add_periodic_task_at<infinite>(
        system_clock::now(), seconds(log_duration), [&]() mutable {
          loaders->map_reduce(connected_log, &client::connected_log);
          loaders->map_reduce(request_log, &client::request_log);
          loaders->map_reduce(send_log, &client::send_log);
          loaders->map_reduce(received_log, &client::received_log);
          client_logger.info("connected: {}\tsend: {}\t request: {}\t received: {}",
                     connected_log.result(), send_log.result(), request_log.result(),
                     received_log.result());
          loaders->invoke_on_all(&client::flush_log_stats);
          connected_log.reset();
          request_log.reset();
          send_log.reset();
          received_log.reset();
      });
			// @ wuwenqing
   		engine().add_oneshot_task_after(seconds(duration + 5), [&] {
      	loaders->stop();
    	});

    }); // engine().add_oneshot_task_at

    }); //when_recved()

    engine().run();
  });
}


