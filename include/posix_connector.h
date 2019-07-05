#pragma once

#include "connector.h"
#include "epoll.h"
#include <vector>
#include <string>

namespace infgen {


class posix_connector : public connector {
public:
  posix_connector() = default;
  virtual connptr connect(socket_address sa, socket_address local) override;
  virtual void configure(boost::program_options::variables_map vm) override;
  struct addr_pool {
    const uint16_t kPortStart = 1025;
    const uint16_t kPortEnd = 65530;

    addr_pool(): ip_index(0), port_index(kPortStart) {}
    std::pair<std::string, uint16_t> get();
    void set_device(std::string& dev);
    void generate_ips();

    std::vector<std::string> ips;
    size_t ip_index;
    uint16_t port_index;
    std::string device;
  };
  virtual void reconnect(connptr conn) override;
  static uint64_t nr_conns_;

private:
  addr_pool addrs_;
  bool no_delay_;
  socket_address fetch_address();
};

} // namespace infgen


