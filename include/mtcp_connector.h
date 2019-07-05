#pragma once

#include "connector.h"

namespace infgen {

class mtcp_connector : public connector {
public:
  mtcp_connector() = default;
  virtual connptr connect(socket_address sa, socket_address local) override;
  virtual void configure(boost::program_options::variables_map vm) override;
  virtual void reconnect(connptr con) override;

private:
  void generate_addrs(int range, socket_address addr);
};
} // namespace infgen
