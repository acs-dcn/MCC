#include "tcp_server.h"
#include "application.h"
#include "reactor.h"

using namespace infgen;

int main(int argc, char** argv) {
  application app;
  app.run(argc, argv, []() {
    ipv4_addr addr("127.0.0.1", 2222);
    auto svr = tcp_server::create_tcp_server(make_ipv4_address(addr));
    if (svr == nullptr) {
      app_logger.error("create server failed");
      exit(EXIT_FAILURE);
    }
    svr->when_recved([] (const connptr& con) {
      con->send_packet("message received!\n");
    });
    engine().run();
  });
}
