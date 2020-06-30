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
#include <random>

using namespace infgen;
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
	int think_time_;		 /// Think time between requests

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

  unsigned Fibonacci_service(int delay) { /// ** ns
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
    
		uint64_t req_done {0};  /// Finished request

		unsigned nr_snd {0};   /// Packets sent
		unsigned nr_rcv {0};  /// Packets received
		unsigned nr_req {0};   /// Requests sent
		unsigned nr_resp {0};  /// Responses received
    
    unsigned header_len;
		unsigned lmode_;
		unsigned imode_;
    unsigned len_;
    bool header_set;
		
		unsigned nr_interact_;      /// times of interaction per connection, default 1
		unsigned lam_;
		std::vector<unsigned> lens_;/// Payload size of each request
		std::vector<unsigned> idts_;/// Payload size of each request

		void cl_stat() {
			nr_snd = 0;
			nr_rcv = 0;
			nr_req = 0;
			nr_resp = 0;
		}

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
			nr_req++;
			nr_snd++;
    }

    void complete_request() {
			nr_resp++;
      req_done++;
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
  std::vector<std::shared_ptr<http_connection>> conns_;
public:

  void running(ipv4_addr server_addr) {
    server_addr_ = server_addr;

		for (unsigned i = 0; i < conn_per_core_; ++i)  {		
			auto conn = engine().connect(make_ipv4_address(server_addr));
			auto http_conn = std::make_shared<http_connection>(conn);
			
			conns_.push_back(http_conn);
			
			conn->when_ready([http_conn, this] (const connptr& conn){
				stats.nr_connected++;
			  http_conn->nr_interact_ = nr_interact_t_;
				http_conn->lmode_ = len_mode_;
				http_conn->imode_ = idt_mode_;
				http_conn->len_ = length_;
				http_conn->lam_ = lambda_;
				http_conn->app_modeling();

				//fmt::print("\033[31 conn {} ready, nr_interact {}.\033[0m\n", conn->get_id(), http_conn->nr_interact_);
				http_conn->do_req();
			});
			
			conn->when_recved([this, http_conn, conn] (const connptr& conn) {
				if (!http_conn->header_set) {
					// parse header
					std::string resp_str = conn->get_input().string();
					http_conn->header_set = true;
				} else {
					std::string content = conn->get_input().string();
				}

				http_conn->nr_rcv++;
			});

			conn->when_closed( [this, conn]{
				stats.nr_connected--;
				/// Close current connection --> Establish another connection
				conn->reconnect(); 
//				engine().add_oneshot_task_after(2s, [conn] {conn->reconnect();});
			});

			conn->on_message([http_conn, this](const connptr& conn, std::string& msg) {
				conn->get_input().consume(msg.size());
				// Simulate 'Think time'	
        unsigned val = Fibonacci_service(think_time_); 
				if (msg.size() > 0)
						msg[0] = static_cast<int>(val % 127);

				http_conn->complete_request();
				
				if (http_conn->nr_interact_ > 0 && 
					conn->get_state() == tcp_connection::state::connected) { // More interactions
					http_conn->do_req();  // Send another request
				} else if (conn->get_state() == tcp_connection::state::connected){ // Finished interacting
					conn->close(); 
				} else {
					fmt::print("\033[31mConn {},  interact_num {}\033[0m\n", conn->get_id(),  http_conn->nr_interact_);			
				}
			});

			conn->when_disconnect([this] (const connptr& conn) {
				stats.nr_connected--;
				//conn->reconnect();
			});
		}
  }

  // methods for map-reduce
	void aggregate_stat() {
		for (auto&& http_conn: conns_) {
			stats.nr_sent += http_conn->nr_snd;
			stats.nr_received += http_conn->nr_rcv;
			stats.nr_request += http_conn->nr_req;
			stats.nr_response += http_conn->nr_resp;
			
			http_conn->cl_stat();
		}
	}

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
				
        for (auto&& http_conn: conns_) {
          //http_conn->finish();
					stats.nr_done += http_conn->req_done;
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
    ("conn,c", bpo::value<unsigned>()->default_value(100), "Total connections")
		("length-mode,g", bpo::value<unsigned>()->default_value(1), "1 for fixed size, 2 for random size")
		("idt-mode,j", bpo::value<unsigned>()->default_value(1), "1 for fixed IDT, 2 for random IDT")
		("length,l", bpo::value<unsigned>()->default_value(16), "Length of message with fixed size(> 8)")
    ("lambda,a", bpo::value<unsigned>()->default_value(32), "Parameter of Poisson distribution")
    ("interact-times,i", bpo::value<unsigned>()->default_value(1), "Times of interactions per connection")
    ("think-time,t", bpo::value<int>()->default_value(100000), "Think time before another request (ns)")
		("verbose,v", bpo::value<unsigned>()->default_value(0), "Show verbose message")
    ("duration,d", bpo::value<unsigned>()->default_value(30), "Duration of test in seconds");
  app.run(argc, argv, [&app] {
    auto &config = app.configuration();
    auto server = config["dest"].as<std::string>();
    auto total_conn = config["conn"].as<unsigned>();
    auto nr_interact = config["interact-times"].as<unsigned>();
		auto len_mode = config["length-mode"].as<unsigned>();
		auto idt_mode = config["idt-mode"].as<unsigned>();
    auto length = config["length"].as<unsigned>();
		auto lambda = config["lambda"].as<unsigned>();
		auto think_time = config["think-time"].as<int>();
		auto verbose = config["verbose"].as<unsigned>();
    auto duration = config["duration"].as<unsigned>();

    if (total_conn % (smp::count-1) != 0) {
      fmt::print("Error, Concurrency needs to be equal to an integral multiple of number of CPUs. \n");
      exit(-1);
    }

		if ((len_mode == 1 && length < 8) || (len_mode == 2 && lambda < 8)) {
			fmt::print("\033[31mWarning, Length of payload should be larger than 8.\033[0m\n");
			exit(-1);
		}

    auto clients = new distributor<http_client>;
    clients->start(total_conn, duration, nr_interact, len_mode, idt_mode, length, lambda, think_time);

    //auto started = system_clock::now();
		fmt::print("\n== WAN Loader ==================================\n");
    fmt::print(" Running {}s test @ server: {}\n", duration, server);
    fmt::print(" Connections: {}\n", total_conn);
		fmt::print("================================================\n\n");

    clients->invoke_on_all(&http_client::running, ipv4_addr(server, 80));

		if (verbose) {
			adder connected, sent, received, request, response;
			engine().add_periodic_task_at<infinite>(
					system_clock::now(), 1s, [&, clients]() mutable {
						clients->invoke_on_all(&http_client::aggregate_stat);
						clients->map_reduce(connected, &http_client::connected_sec);
						clients->map_reduce(sent, &http_client::sent_sec);
						clients->map_reduce(received, &http_client::received_sec);
						clients->map_reduce(request, &http_client::request_sec);
						clients->map_reduce(response, &http_client::response_sec);
						clients->invoke_on_all(&http_client::print_stats);
					
						engine().add_oneshot_task_after(150ms, [&] () mutable {
            fmt::print("[ALL]\t\tconnected: {}\tsend: {}\treceive: {}\trequest: {}\tresponse: {}\n",
                       connected.result(), 
											 sent.result(), received.result(), request.result(), response.result());
            connected.reset();
            sent.reset();
            received.reset();
            request.reset();
            response.reset();
            fmt::print("\n");
          });
			});	
		}
	
		system_clock::time_point start_ts;
		system_clock::time_point end_ts;
    
		engine().add_oneshot_task_after(3s, [clients, &start_ts]() {
      clients->invoke_on_all(&http_client::end_test);
			start_ts = system_clock::now();
    });

		clients->invoke_on_all(&http_client::end_test);

		adder total_requests;
		clients->when_done([&end_ts, clients, &total_requests] () mutable {
			app_logger.info("Load test finished.\nAggregating statistics...");
			end_ts = system_clock::now();
			clients->map_reduce(total_requests, &http_client::total_reqs);

			engine().add_oneshot_task_after(1s, [clients] {
//			  clients->stop();
				engine().stop();	
			});
		});

    engine().run();

		auto reqs = total_requests.result();
		std::chrono::duration<double> elapsed = end_ts - start_ts;
		auto secs = elapsed.count();

		fmt::print("\n== WAN Loader ==================================\n");
		fmt::print(" Test Done\n");				
		fmt::print(" {} requests in {}s\n", reqs, secs);
		fmt::print(" Transfer / sec: {}\n", static_cast<double>(reqs) / secs);				
		fmt::print("================================================\n\n");

    delete clients;

  });

  return 0;
}
