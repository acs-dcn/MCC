#include "reactor.h"
#include "log.h"
#include "application.h"

using namespace infgen;
using namespace infgen::net;
namespace bpo = boost::program_options;

int main(int argc, char** argv) {
  application app;
  app.add_options()
    ("rate,r", bpo::value<unsigned>()->default_value(10), "connection rate");
  app.run(argc, argv, [&app]() {
    auto config = app.configuration();
    auto rate = config["rate"].as<unsigned>();
    auto dest = config["dest"].as<std::string>();
    ipv4_addr addr(dest, 1080);
    unsigned connected = 0;
    engine().add_periodic_task_at<infinite>(system_clock::now() + 2s, 1000ms, [&] {
      for (unsigned i = 0; i < rate; i++) {
        auto conn = engine().connect(make_ipv4_address(addr));
        conn->when_ready([&connected](const connptr& conn) mutable {
          //conn->send_packet("hello");
          connected++;
          //conn->close();
        });
        conn->when_closed([&connected]() mutable {
          connected--;
        });
        conn->when_disconnect([&connected](const connptr& conn) mutable {
          connected--;
          conn->reconnect();
        });
      }
    });
    engine().add_periodic_task_at<infinite>(system_clock::now(), 1s, [&connected] {
      app_logger.info("connected: {}", connected);
    });

    engine().run();
  });
}




