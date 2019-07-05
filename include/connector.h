#pragma once
#include "inet_addr.h"
#include <memory>
#include <functional>
#include <boost/program_options.hpp>

namespace infgen {

class tcp_connection;

using connptr = std::shared_ptr<tcp_connection>;

class connector {
public:
  connector() = default;
  connector(connector&) = delete;
  void operator=(connector&) = delete;
  virtual ~connector() {}
  virtual connptr connect(socket_address sa, socket_address local=socket_address{}) = 0;
  virtual void configure(boost::program_options::variables_map vm) = 0;
  virtual void reconnect(connptr con) = 0;
};

} // namespace infgen
