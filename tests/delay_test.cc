#include "application.h"
#include "log.h"
#include "reactor.h"
#include <iostream>

using namespace infgen;

int main(int argc, char** argv) {
  application app;
  app.run(argc, argv, []() {
    system_clock::time_point send_ts, recv_ts;
    ipv4_addr addr("192.168.2.1", 80);
    // std::vector<std::shared_ptr<tcp_connection>> conns;
    auto conn = engine().connect(make_ipv4_address(addr));
    conn->when_ready([&](const connptr& conn) {
      engine().add_periodic_task_at<infinite>(system_clock::now(), 1s, [conn, &send_ts] {
        conn->send_packet(
            "GET / HTTP/1.1\r\n"
            "User-Agent: Wget/1.12 (linux-gnu)\r\n"
            "HOST: 192.168.2.2\r\n\r\n");
        send_ts = system_clock::now();
      });
    });

    conn->on_message([&](const connptr& conn, std::string& msg) {
      recv_ts = system_clock::now();
      auto delay = duration_cast<microseconds>( recv_ts - send_ts);
      app_logger.info("delay {} us", delay.count());
      //conn->close();
      std::cout << msg << std::endl;
    });

    engine().run();
  });
}
