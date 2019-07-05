#include "log.h"
#include <iostream>

using namespace infgen;
int main() {
  fmt::print("normal fmt print!\n");
  logger test_logger("test");
  test_logger.error("This is an error");
  const char message[] = "HIDDEN MAN";
  test_logger.error("Hello, this is a formatted error: message is [{}]", message);
  test_logger.warn("This is a warn");
  test_logger.trace("This trace message should no be visible");

  logger::get_logger().info("default logger trace");
  logger::get_logger().error("default logger error");
}


