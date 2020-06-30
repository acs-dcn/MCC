#include "application.h"
#include "connection.h"
#include "log.h"
#include "reactor.h"
#include "smp.h"
#include "distributor.h"
#include "http/http_parser.h"
#include "proto/wan.pb.h"

#include <chrono>
#include <memory>
#include <vector>
#include <list>
#include <random>

using namespace infgen;
using namespace std::chrono;
using namespace std::chrono_literals;
namespace bpo = boost::program_options;

class http_client {
private:
	unsigned total_conn_;
  unsigned duration_;
  unsigned conn_per_core_;
  uint64_t conn_finished_{0};

	unsigned nr_interact_t_; /// times of interaction per connection, default 1
	unsigned len_mode_;	 		 /// Fixed length or Random length
	unsigned idt_mode_;	 		 /// Fixed inter-departure time or Random IDT
	unsigned length_;				 /// Length of payload
	unsigned lambda_;        /// Parameter of Poisson Distribution
	int think_time_;         /// Think time between requests (ns)

  ipv4_addr server_addr_;
  distributor<http_client>* container_;

  struct metrics {
		uint64_t nr_done;
		unsigned nr_connected;
	
		unsigned nr_sent;
		unsigned nr_received;

		unsigned nr_request;
		unsigned nr_response;
  };

	metrics stats;
		
	void clear_stats(metrics& stats) {
		stats.nr_sent = 0;
		stats.nr_received = 0;
		stats.nr_request = 0;
		stats.nr_response = 0;
	}

  unsigned Fibonacci_service(int delay) {  /// ns
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
    
    unsigned header_len;
		unsigned lmode_;
		unsigned imode_;
    unsigned len_;
    bool header_set;
		
		unsigned nr_interact_;      /// times of interaction per connection, default 1
		unsigned lam_;
		std::vector<unsigned> lens_;/// Payload size of each request
		std::vector<unsigned> idts_;/// Payload size of each request

		void app_modeling() {
			std::minstd_rand engine(time(NULL));
			std::poisson_distribution<unsigned> distribution(lam_);

			unsigned tmp = distribution(engine);
			idts_.push_back(tmp + 2); //start time
			lens_.push_back(tmp % 513);
			for (unsigned i = 1; i < nr_interact_; ++i) {
				tmp = distribution(engine);
				lens_.push_back(tmp % 513);
				idts_.push_back(idts_[i-1] + tmp);
			}
		}

		void do_req() {
      //flow->send_packet("GET / HTTP/1.1\r\n"
      //    "HOST: 192.168.2.2\r\n\r\n");
			std::string req;
			size_t size = lens_.size();
			if (lmode_ == 1) {
				req = std::string(len_, '0');
			} else {
				req = std::string(lens_[size - nr_interact_], '0');
			}
			// Write timestamp
			if (imode_ == 1) {
			} else {
				char * rptr = req.data();
				memcpy((void *)rptr, &idts_[size - nr_interact_], 4);
			}

			req[5] = 0x01;
			req[6] = 0x02;

      flow->send_packet(req);

			nr_interact_--;
    }

    void complete_request() {
    }

    void finish() {
			// Modify close callback
      flow->when_closed([this] {
        app_logger.trace("Test complete, connection {} closed", flow->get_id());
      });
      
			flow->close();
    }
  };

public:
  http_client(unsigned concurrency, unsigned duration,
							unsigned nr_interact, unsigned len_mode, unsigned idt_mode, 
							unsigned length, unsigned lambda, int think_time)
      : total_conn_(concurrency), duration_(duration), conn_per_core_(concurrency / (smp::count-1)), 
			nr_interact_t_(nr_interact), len_mode_(len_mode), idt_mode_(idt_mode),
			length_(length), lambda_(lambda), think_time_(think_time){
		stats.nr_done = 0;
		stats.nr_connected = 0;
		stats.nr_sent = 0;
		stats.nr_received = 0;
		stats.nr_request = 0;
		stats.nr_response = 0;
  }

private:
  std::list<std::shared_ptr<http_connection>> conns_;
public:
 void Reconnecting(ipv4_addr server_addr) {
    engine().add_periodic_task_at<infinite>(system_clock::now(), 100ms,
            [&, server_addr]() mutable {
      assert(stats.nr_connected >= 0); 
      while (stats.nr_connected < conn_per_core_) {
          auto conn = engine().connect(make_ipv4_address(server_addr));
          auto http_conn = std::make_shared<http_connection>(conn);
    
          stats.nr_connected++;
          conns_.push_back(http_conn);
    
          conn->when_ready([http_conn, this] (const connptr& conn){
    
            http_conn->nr_interact_ = nr_interact_t_;
            http_conn->lmode_ = len_mode_;
            http_conn->imode_ = idt_mode_;
            http_conn->len_ = length_;
            http_conn->lam_ = lambda_;
            http_conn->app_modeling();

            http_conn->do_req();
            stats.nr_sent++;
            stats.nr_request++;
          });
         conn->when_recved([this, http_conn, conn] (const connptr& conn) {
            if (!http_conn->header_set) {
              // parse header
              std::string resp_str = conn->get_input().string();
              http_conn->header_set = true;
            } else {
              std::string content = conn->get_input().string();
            }

            stats.nr_received++;
          });

          conn->when_closed( [this, http_conn, conn] {
            stats.nr_connected--;
            //conn->reconnect(); /// Close current connection --> Establish another connection          
						conns_.remove(http_conn);
					});

          conn->on_message([http_conn, this](const connptr& conn, std::string& msg) { 
						conn->get_input().consume(msg.size());
            // Simulate 'Think time'  
            unsigned val = Fibonacci_service(think_time_);
            if (msg.size() > 0)
                msg[0] = static_cast<int>(val % 127);

            http_conn->complete_request();
            stats.nr_response++;
            stats.nr_done++;

            if (http_conn->nr_interact_ > 0 &&
              conn->get_state() == tcp_connection::state::connected) { // More interactions              
							http_conn->do_req();  // Send another request
              stats.nr_sent++;
              stats.nr_request++;
            } else if (conn->get_state() == tcp_connection::state::connected){ 
								// Finished interacting              
							conn->close();
              conns_.remove(http_conn);
            } else {
              fmt::print("\033[31mConn {},  interact_num {}\033[0m\n", conn->get_id(),  http_conn->nr_interact_);            }
          });

          conn->when_disconnect([this, http_conn] (const connptr& conn) {
            stats.nr_connected--;
						conns_.remove(http_conn);
            //conn->reconnect();
          });
        } // while (stats.nr_connected < conn_per_core_)
    });
  }

  void running(ipv4_addr server_addr) {
    server_addr_ = server_addr;

		for (unsigned i = 0; i < conn_per_core_; ++i)  {		
			auto conn = engine().connect(make_ipv4_address(server_addr));
			
			auto http_conn = std::make_shared<http_connection>(conn);
			
		  stats.nr_connected++;
			conns_.push_back(http_conn);
			
			conn->when_ready([http_conn, this] (const connptr& conn){
			  http_conn->nr_interact_ = nr_interact_t_;
				http_conn->lmode_ = len_mode_;
				http_conn->imode_ = idt_mode_;
				http_conn->len_ = length_;
				http_conn->lam_ = lambda_;
				http_conn->app_modeling();

				//fmt::print("\033[31 conn {} ready, nr_interact {}.\033[0m\n", conn->get_id(), http_conn->nr_interact_);
				http_conn->do_req();
        stats.nr_sent++;
        stats.nr_request++;
			});
			
			conn->when_recved([this, http_conn, conn] (const connptr& conn) {
				if (!http_conn->header_set) {
					// parse header
					std::string resp_str = conn->get_input().string();
					http_conn->header_set = true;
				} else {
					std::string content = conn->get_input().string();
				}
				stats.nr_received++;
			});

			conn->when_closed( [this, http_conn, conn] {
				stats.nr_connected--;
				conns_.remove(http_conn);
			//	conn->reconnect(); /// Close current connection --> Establish another connection
			});

			conn->on_message([http_conn, this](const connptr& conn, std::string& msg) {
				conn->get_input().consume(msg.size());
				http_conn->complete_request();
        stats.nr_response++;
        stats.nr_done++;
				
				//@ wuwenqing, simulate 'think time'
        unsigned val = Fibonacci_service(think_time_); 
				if (msg.size() > 0)
						msg[0] = static_cast<int>(val % 127);

				if (http_conn->nr_interact_ > 0 && 
					conn->get_state() == tcp_connection::state::connected) { // More interactions
					http_conn->do_req();  // Send another request
          
					stats.nr_sent++;
          stats.nr_request++;
				} else if (conn->get_state() == tcp_connection::state::connected){ // Finished interacting
					conn->close(); 
          conns_.remove(http_conn);
				} else {
					fmt::print("\033[31mConn {},  interact_num {}\033[0m\n", conn->get_id(),  http_conn->nr_interact_);			
				}
			});

			conn->when_disconnect([this, http_conn] (const connptr& conn) {
				stats.nr_connected--;
				//conn->reconnect();
				conns_.remove(http_conn);
			});
		} // for(...)

    engine().add_oneshot_task_after (1s, [this, server_addr]{ 
			Reconnecting(server_addr); 
		});
  } // running(...)

	uint64_t total_reqs() {
		fmt::print("Request on cpu {}: {}\n", engine().cpu_id(), stats.nr_done);
		return stats.nr_done;
	}

	uint64_t connected_sec() { return stats.nr_connected; }
	uint64_t sent_sec() { return stats.nr_sent; }
	uint64_t received_sec() { return stats.nr_received; }
	uint64_t request_sec() { return stats.nr_request; }
	uint64_t response_sec() { return stats.nr_response; }
  
	void print_stats() {
    fmt::print("[engine {}]\tconnected: {} \tsend: {}\treceive: {}\t"
                 "request: {}\tresponse: {}\n", engine().cpu_id(),
                stats.nr_connected, stats.nr_sent, stats.nr_received, 
								stats.nr_request, stats.nr_response);
    clear_stats(stats);
  }

  void finish() {
    app_logger.info("loader {} finished", engine().cpu_id());
    container_->end_game(this);
  }

  void set_container(distributor<http_client>* container) {
    container_ = container;
  }

  void end_test() {
    if (duration_ > 0) {
      engine().add_oneshot_task_after(std::chrono::seconds(duration_), [this] {
#if 0	
        for (auto&& http_conn: conns_) {
          //http_conn->finish();
					stats.nr_done += http_conn->req_done;
        }
#endif		
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
		("verbose,v", bpo::value<unsigned>()->default_value(0), "Show verbose message")
    ("server-ip,s", bpo::value<std::string>(), "server ip address")
    ("server-port,p", bpo::value<unsigned>()->default_value(2222), "server port")    
		("local-ip,l", bpo::value<std::string>(), "local ip address")
    ("client-id,n", bpo::value<unsigned>(), "client id");

  app.run(argc, argv, [&app] {
    auto &config = app.configuration();
    auto dest = config["dest"].as<std::string>();
		auto verbose = config["verbose"].as<unsigned>();
    auto ip = config["server-ip"].as<std::string>();
    auto port = config["server-port"].as<unsigned>();
    auto local_ip = config["local-ip"].as<std::string>();
    auto id = config["client-id"].as<unsigned>();

    ipv4_addr server_addr(ip, port);
    ipv4_addr local_addr(local_ip);

		uint32_t conns, duration;
		int think_time;
		uint32_t len_mode, length, idt_mode, lambda, nr_interact;
		uint64_t start_ts;
		system_clock::time_point start_tp;
		
    system_clock::time_point started, finished;
    adder reqs;

    auto clients = new distributor<http_client>;
    
    auto conn = engine().connect(make_ipv4_address(server_addr), make_ipv4_address(local_addr));    
		
		conn->when_ready([&](const connptr& con) {
      app_logger.info("Connected to server!");
      report r;
      r.set_client_id(id);
      report::notice *n = r.mutable_note();
      n->set_online(true);
      std::string packet;
      r.SerializeToString(&packet);
      conn->send_packet(packet); // Report status
    }); 

    conn->on_message([&](const connptr& conn, std::string msg) mutable {
      command cmd;
      conn->get_input().consume(msg.size());
      if (!cmd.ParseFromString(msg)) {
        app_logger.error("Failed to parse message!");
      }

      conns = cmd.conn();
      duration = cmd.duration();
      start_ts = cmd.start_ts();
			len_mode = cmd.length_mode();
			length = cmd.length();
			idt_mode = cmd.idt_mode();
			lambda = cmd.lambda();
			nr_interact = cmd.interact_times();
			think_time = cmd.think_time();

      start_tp = start_tp + milliseconds(start_ts);

      fmt::print(
          "configuration: \n\tconnections: {}\n\tduration: {}\n\tthreads:{}\n",          
					conns, duration, smp::count-1);
      if (conns % (smp::count-1) != 0) {
        fmt::print("error: conn needs to be n * cpu_nr \n");
        exit(-1);
      }

			if (conns % (smp::count-1) != 0) {
  	    fmt::print("Error, Concurrency needs to be equal to an integral multiple of number of CPUs. \n");
    	  exit(-1);
    	}

			if ((len_mode == 1 && length < 8) || (len_mode == 2 && lambda < 8)) {
				fmt::print("\033[31mWarning, Length of payload should be larger than 8.\033[0m\n");
				exit(-1);
			}

      engine().add_oneshot_task_at(start_tp, [&, conn]() mutable {
				clients->start(conns, duration, nr_interact, len_mode, idt_mode, length, lambda, think_time);

        started = system_clock::now();
        fmt::print("Concurrency set: {}\n", conns);

        clients->invoke_on_all(&http_client::running, ipv4_addr(dest, 80));
        clients->invoke_on_all(&http_client::end_test);
				
				if (verbose) {
					adder connected, sent, received, request, response;
					engine().add_periodic_task_at<infinite>(
							system_clock::now(), 1s, [&, clients]() mutable {
								clients->map_reduce(connected, &http_client::connected_sec);
								clients->map_reduce(sent, &http_client::sent_sec);
								clients->map_reduce(received, &http_client::received_sec);
								clients->map_reduce(request, &http_client::request_sec);
								clients->map_reduce(response, &http_client::response_sec);
								clients->invoke_on_all(&http_client::print_stats);
							
								engine().add_oneshot_task_after(200ms, [&] () mutable {
								fmt::print("[ALL]\t\tconnected: {}\tsend: {}\treceive: {}\trequest: {}\tresponse: {}\n",
									connected.result(), sent.result(), received.result(), request.result(), response.result());
								connected.reset();
								sent.reset();
								received.reset();
								request.reset();
								response.reset();
								fmt::print("\n");
							});
					});	
				}

        finished = started;

        clients->when_done([&, clients, conn]() mutable {
          app_logger.info("load test finished, running statistics collection process...");          
					finished = system_clock::now();
          clients->map_reduce(reqs, &http_client::total_reqs);

          engine().add_oneshot_task_after(1s, [&, clients, conn] {
            auto total_reqs = reqs.result();
            auto elapsed = duration_cast<seconds>(finished - started);
            auto secs = elapsed.count();
						fmt::print("\n== WAN Loader ==================================\n");
            fmt::print("Total CPUs: {}\n", smp::count-1);
						fmt::print("{} requests in {}s\n", total_reqs, secs);          
						fmt::print("Request/sec:  {}\n", static_cast<double>(total_reqs) / secs);
            fmt::print("================================================\n\n");

            report r;
            r.set_client_id(id);
            r.set_completes(total_reqs);
            std::string packet;
            r.SerializeToString(&packet);
            conn->send_packet(packet); // Report final statistics

            engine().add_oneshot_task_after(1s, [&, clients, conn] {
//              clients->stop();
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
