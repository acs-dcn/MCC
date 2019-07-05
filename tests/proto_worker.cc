#include "proto/mcc.pb.h"
#include "application.h"
#include "reactor.h"
#include "tcp_server.h"

using namespace infgen;
namespace bpo = boost::program_options;

int main(int argc, char* argv[]) {
  application app;
  app.add_options()
    ("client-id,n", bpo::value<unsigned>()->default_value(0), "client id");
  app.run(argc, argv, [&app]() {
    // worker node
    auto &config = app.configuration();
    auto id = config["client-id"].as<unsigned>();
    ipv4_addr addr("10.30.10.233", 2222);
    pin_this_thread(1);
    auto conn = engine().connect(make_ipv4_address(addr));
    conn->when_ready([&](const connptr& conn) {
      report r;
      r.set_client_id(id);
      report::notice* n = r.mutable_note();
      n->set_online(true);
      std::string packet;
      if (!r.SerializeToString(&packet)) {
        app_logger.error("serialize message failed");
        exit(-1);
      }
      conn->send_packet(packet);
    });
    engine().run();
  });
}
