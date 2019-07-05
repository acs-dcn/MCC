#include <fstream>
#include <iostream>
#include "application.h"
#include "log.h"
#include "reactor.h"

using namespace infgen;

int main(int argc, char** argv) {
  application app;

  app.run(argc, argv, [&app]() {
    ipv4_addr addr("192.168.2.1", 1080);
    auto conf = app.configuration();
    auto dest = conf["dest"].as<std::string>();
    auto stack = conf["network-stack"].as<std::string>();
    if (stack != "mtcp") {
      app_logger.error("accurate test is only available using mtcp stack!");
      engine().stop();
    }
    auto conn = engine().connect(make_ipv4_address(addr));
    conn->when_ready([&](const connptr& conn) {
      engine().add_periodic_task_at<infinite>(
          system_clock::now() + 1s, 1s, [conn] {
            for (int i = 0; i < 2000000; i++) {
              conn->send_packet(
                  "GET / HTTP/1.1\r\n"
                  "User-Agent: Wget/1.12 (linux-gnu)\r\n"
                  "HOST: 192.168.2.2\r\n\r\n");
            }
          });
    });

    conn->on_message([&](const connptr& conn, std::string& msg) {
      std::cout << msg << std::endl;
    });

    engine().run();
  });
}
