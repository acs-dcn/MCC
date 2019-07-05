#include "reactor.h"
#include "application.h"

using namespace infgen;

int main(int argc, char** argv) {
  application app;
  app.run(argc, argv, []() {
    int counter = 0;
    engine().add_periodic_task_after<infinite>(std::chrono::seconds(1), [] {
      app_logger.info("hello from timer");
    });

    engine().add_periodic_task_at<infinite>(system_clock::now()+1000ms, 1000ms, [&] {
      app_logger.info("task 1 crossing");
    });

    engine().add_periodic_task_at<infinite>(system_clock::now()+1500ms, 1000ms, [&] {
      app_logger.info("task 2 crossing");
      app_logger.info("counter: {}", counter);
    });

    engine().add_periodic_task_at<infinite>(system_clock::now()+1000ms, 1ms, [&] {
      for (unsigned i = 0; i < 100000; i++) {
        counter++;
      }
    });

    engine().run();
  });
}

  


