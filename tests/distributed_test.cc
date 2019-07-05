#include "application.h"
#include "distributor.h"
#include "log.h"

#include <iostream>

using namespace infgen;
using namespace infgen::net;

struct X {
public:
  X(int i): value(i) {
    app_logger.info("Instance of X running"); 
  }
  uint64_t cpu_id() {
    auto id = engine().cpu_id();
    return id;
  }

  void stop() {
    engine().stop(); 
  }

  void set_container(distributor<X>* c) {
    container = c; 
  }
  void end_game() {

  }
private:
  int value;
  distributor<X>* container;
};

int main(int argc, char **argv) {
  application app;
  return app.run(argc, argv, []() {
    app_logger.info("marker");
    distributor<X> cluster;
    cluster.start(1);
    cluster.invoke_on_all([=] {
      app_logger.info("distributed hello from core {}", engine().cpu_id());
    });

   adder reducer;
   cluster.map_reduce(reducer, &X::cpu_id);
   engine().add_oneshot_task_after(std::chrono::seconds(2), [&cluster] {
     cluster.stop();
   });
   engine().add_oneshot_task_after(std::chrono::seconds(3), [&cluster] {
     engine().stop();
   });

   engine().run();
   auto result = reducer.result();
   std::cout << "final result is " << result << std::endl;
  });
}
