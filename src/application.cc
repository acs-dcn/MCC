#include "application.h"
#include "smp.h"
#include "io_sched.h"
#include "mtcp_api.h"

#include <fstream>
#include <iostream>

namespace infgen {

namespace bpo = boost::program_options;

logger app_logger("app");

std::string network_stack;


application::application(application::config cfg)
    : cfg_(std::move(cfg)), opts_(cfg_.name + " options"),
      conf_reader_(get_default_configuration_reader()) {
  opts_.add_options()
    ("help,h", "show help message")
    ("log", bpo::value<std::string>()->default_value("info"), "set log level");

  sub_opts_.add(reactor::get_options_description());
  sub_opts_.add(smp::get_options_description());
  sub_opts_.add(io_scheduler::get_options_description());
  opts_.add(sub_opts_);
}

application::configuration_reader
application::get_default_configuration_reader() {
  return [this](bpo::variables_map &configuration) {
    auto pwd = std::getenv("PWD");
    if (pwd) {
      std::ifstream ifs(std::string(pwd) + "/config/infgen.conf");
      if (ifs) {
        bpo::store(bpo::parse_config_file(ifs, opts_), configuration);
      }
    }
  };
}

bpo::options_description_easy_init application::add_options() {
  return opts_.add_options();
}

bpo::variables_map &application::configuration() { return *configuration_; }

int application::run(int ac, char **av, std::function<void()> &&func) {
  bpo::variables_map configuration;
  try {
    bpo::store(bpo::parse_command_line(ac, av, opts_), configuration);
    conf_reader_(configuration);
  } catch (bpo::error &e) {
    fmt::print("error: {}\n\nTry --help\n", e.what());
    return 2;
  }
  if (configuration.count("help")) {
    std::cout << opts_ << "\n";
    return 1;
  }

  auto log_level = configuration["log"].as<std::string>();
  global_logger_registry().set_all_logger_level(log_level);

//  auto stack = configuration["network-stack"].as<std::string>();

  bpo::notify(configuration);
  try {
    smp::configure(configuration);
  } catch (std::exception &e) {
    std::cerr << "could not initialize infgen: " << e.what() << std::endl;
    return 1;
  }
  configuration_ = {std::move(configuration)};
  func();
  smp::cleanup();
  return 0;
}
} // namespace infgen
