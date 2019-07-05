#include "proto/mcc.pb.h"
#include "application.h"
#include "reactor.h"
#include "tcp_server.h"

using namespace infgen;

int main(int argc, char* argv[]) {
  application app;
  app.run(argc, argv, []() {
      // master node 
    ipv4_addr addr("10.30.10.233", 2222);
    auto svr = tcp_server::create_tcp_server(make_ipv4_address(addr));
    if (svr == nullptr) {
      app_logger.error("create server failed");
      exit(-1);
    }

    svr->when_disconnect([] (const connptr& con) {
      app_logger.info("client {} offline", con->get_id()); 
    });

    svr->when_recved([] (const connptr& con) {
      report r;
      std::string s = con->get_input().string();

      con->get_input().consume(s.size());

      if (!r.ParseFromString(s)) {
        app_logger.error("failed to parse message");
        exit(-1);
      }

      if (r.has_note()) {
        // the first packet is for notice
        auto client_id = r.client_id();
        con->set_id(client_id);
        app_logger.info("client {} online", client_id);
      } else {
        // do the stat report
      }
    });
    engine().run();
  });
}
