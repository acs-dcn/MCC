#include "application.h"
#include "log.h"
#include "smp.h"
#include "distributor.h"


using namespace infgen;

system_clock::time_point timewall;

class test_gen {
  std::vector<connptr> conns_;
  unsigned nr_conns_;
  unsigned interval_;
  distributor<test_gen>* container_;
public:
  test_gen(unsigned conns, unsigned interval) : nr_conns_(conns), interval_(interval) {}
  void connect() {
    engine().add_oneshot_task_at(timewall, [this] {
      ipv4_addr addr("192.168.1.1", 80);
      app_logger.info("start to connect");
      for (unsigned i = 0; i < nr_conns_; i++) {
        auto conn = engine().connect(make_ipv4_address(addr));
        conn->when_ready([this] (const connptr& conn) {
          conns_.push_back(conn);
          if (conns_.size() >= nr_conns_) {
            app_logger.info("start to send messages");
            do_req();
          }
        });
      }
    });
  }

  void do_req() {
    engine().add_periodic_task_at<infinite>(system_clock::now(), seconds(interval_), [this] {
      for (auto&& conn: conns_) {
        conn->send_packet("hello");
      }
    });
  }

  void set_container(distributor<test_gen>* c) {
    container_ = c;
  }

  void end_game() {}
};

int main(int argc, char **argv) {
  application app;
  timewall = system_clock::now() + 20s;
  app.run(argc, argv, []() {
    distributor<test_gen> tests;
    tests.start(2000, 1);
    tests.invoke_on_all(&test_gen::connect);
    app_logger.info("engine ready to start");
    engine().run();
  });
}
