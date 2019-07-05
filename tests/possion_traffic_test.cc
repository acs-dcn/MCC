#include "application.h"
#include "log.h"
#include "reactor.h"
#include <iostream>
#include <fstream>

using namespace infgen;

int main(int argc, char** argv) {
  application app;

  app.run(argc, argv, []() {

    std::vector<int> intervals;
    std::vector<uint64_t> timestamps;
    std::ifstream input("intervals.txt", std::ios::in);
    if (input.is_open()) {
      int ts;
      while (input >> ts) {
        intervals.push_back(ts);
      }
    } else {
      app_logger.error("Open parameter file failed!");
      exit(-1);
    }

    timestamps.push_back(intervals[0]);
    for (size_t i = 1; i < intervals.size(); i++) {
      timestamps.push_back(timestamps[i-1] + intervals[i]);
    }
    ipv4_addr addr("192.168.240.1", 1080);
    auto conn = engine().connect(make_ipv4_address(addr));
    conn->when_ready([&](const connptr& conn) {
      engine().add_oneshot_task_after(1s, [conn, &timestamps] {
        engine().add_oneshot_task_after(milliseconds(timestamps.back() + 5000), [] {
          app_logger.info("All packets delivered!");
          engine().stop();
        });

        for (auto t: timestamps) {
          engine().add_oneshot_task_after(milliseconds(t), [conn,t] {
            conn->send_packet(
                "GET / HTTP/1.1\r\n"
                "User-Agent: Wget/1.12 (linux-gnu)\r\n"
                "HOST: 192.168.2.2\r\n\r\n");
          });
        }
      });
    });

    engine().run();
  });
}
