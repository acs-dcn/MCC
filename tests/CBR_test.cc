#include "application.h"
#include "log.h"
#include "reactor.h"
#include <iostream>
#include <fstream>

using namespace infgen;

int main(int argc, char** argv) {
  application app;

  app.run(argc, argv, []() {
    
    ipv4_addr addr("192.168.2.1", 1080);
    unsigned count = 0;
    auto conn = engine().connect(make_ipv4_address(addr));
    conn->when_ready([&](const connptr& conn) {
      engine().add_oneshot_task_after(60s, [] {
        app_logger.info("All packets delivered!");
        engine().stop();
      });

      engine().add_periodic_task_at<infinite>(system_clock::now() + 1s,
        100us, [conn, &count] {
        conn->send_packet(
          "GET / HTTP/1.1\r\n"
          "User-Agent: Wget/1.12 (linux-gnu)\r\n"
          "HOST: 192.168.2.2\r\n\r\n");
        count++;
      });
    });

    conn->on_message([&](const connptr& conn, std::string& msg) {
      std::cout << msg << std::endl;
    });

    engine().add_periodic_task_at<infinite>(system_clock::now(), 1s, [&count] {
      fmt::print("Delivered packets: {}\n", count);
      count = 0;
    });

    engine().run();
  });
}
