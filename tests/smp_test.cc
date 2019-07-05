#include "application.h"
#include "log.h"
#include "smp.h"

using namespace infgen;

int main(int argc, char **argv) {
  application app;
  app.run(argc, argv, []() {
    smp::submit_to(0, [] {
      app_logger.info("hello from core {}",engine().cpu_id());
    });
    smp::submit_to(1, [] {
      app_logger.info("hello from core {}", engine().cpu_id());
    });

    smp::submit_to(1, [] {
      sleep(5);
    }, [] {
      app_logger.info("5 seconds later, callback called");
      engine().stop();
    });

    smp::submit_to(2, [] {
      sleep(3);
      return 1;
    }, [] (unsigned result) {
      app_logger.info("3 seconds later, callback func called on result {}", result);
    });


    engine().run();
  });
}
