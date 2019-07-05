#include "reactor.h"
#include "application.h"


using namespace infgen;

int main(int argc, char* argv[]) {
  application app;
  app.run(argc, argv, [&app] {
    auto config = app.configuration();
    auto dest = config["dest"].as<std::string>();
    ipv4_addr serv_addr(dest, 1080);
    unsigned done = 0;
    auto conn = engine().connect(make_ipv4_address(serv_addr));
    conn->when_ready([] (const connptr& conn) {
      engine().add_oneshot_task_after(1s, [conn] {
        conn->send_packet("hello");
      });
    });

    conn->on_message([&done] (const connptr& conn, std::string& msg) mutable {
      app_logger.trace("message assembled");
      done++;
      conn->close();
    });

    conn->when_closed([conn] {
      app_logger.info("connection closed");
      conn->reconnect();
      app_logger.info("connection reconnecting");
    });

    auto start = system_clock::now();
    engine().run();
    auto end = system_clock::now();
    auto cost = duration_cast<milliseconds>(end-start).count();

    fmt::print("done: {}\n cost: {}ms\n", done, cost);

  });
}
