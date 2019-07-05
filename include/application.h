#pragma once

#include <functional>
#include <string>

#include <boost/program_options.hpp>
#include <boost/optional.hpp>

#include "log.h"

namespace infgen {

extern logger app_logger;

class application {
public:
  using configuration_reader =
      std::function<void(boost::program_options::variables_map &)>;
  struct config {
    std::string name = "App";
    config() {}
  };

private:
  config cfg_;
  boost::program_options::options_description opts_;
  boost::program_options::options_description sub_opts_;
  boost::optional<boost::program_options::variables_map> configuration_;

  configuration_reader conf_reader_;
  configuration_reader get_default_configuration_reader();

public:
  boost::program_options::variables_map& configuration();
  boost::program_options::options_description_easy_init add_options();
  explicit application(config cfg=config());
  /// Notice: func must call engine.run() to start engine on the main
  /// thread!
  int run(int ac, char **av, std::function<void()> &&func);
};
} // namespace infgen
