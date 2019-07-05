#include "log.h"
#include "posix_connection.h"
#include "posix_connector.h"

namespace infgen {

extern logger net_logger;

uint64_t posix_connector::nr_conns_ = 0;

void posix_connector::configure(boost::program_options::variables_map vm) {
  std::string dev;
  if (vm.count("device")) {
    dev = vm["device"].as<std::string>();
    addrs_.set_device(dev);
  } else {
    dev = "";
    net_logger.warn("no network device is specified!");
  }
  no_delay_ = vm["no-delay"].as<bool>();
}

connptr posix_connector::connect(socket_address sa, socket_address lo) {
  nr_conns_++;
  file_desc fd;
  try {
    fd = file_desc::socket(sa.u.in.sin_family,
                           SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    posix_socket::set_no_delay(fd.get(), no_delay_);
  } catch (std::system_error &e) {
    net_logger.error("socket error: {}", e.what());
  }

  socket_address local;

  if (lo.any) {
  // generate local address from address pool
  bind:
    try {
      local = fetch_address();
      fd.bind(local.u.sa, sizeof(local.u.sas));
    } catch (std::system_error &e) {
      net_logger.trace("bind error, rebind to another address");
      goto bind;
    }
  } else {
  // local address is set by caller
    local = lo;
    fd.bind(local.u.sa, sizeof(local.u.sas));
  }

  net_logger.trace("connecting {} from {}", sa, local);
  try {
    fd.connect(sa.u.sa, sizeof(sa.u.sas));
  } catch (std::system_error& e) {
    net_logger.error("connect error: {}", e.what());
    engine().stop();
  }

  auto con = std::make_shared<posix_connection>();
  con->attach(fd.get(), local, sa);
  return con;
}

void posix_connector::reconnect(connptr old_conn) {
  file_desc fd;
  socket_address peer = old_conn->get_peer();
  try {
    fd = file_desc::socket(peer.u.in.sin_family,
                           SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    posix_socket::set_no_delay(fd.get(), no_delay_);
  } catch (std::system_error &e) {
    net_logger.error("socket error: {}", e.what());
  }

  socket_address local;
bind:
  try {
    local = fetch_address();
    fd.bind(local.u.sa, sizeof(local.u.sas));
  } catch (std::system_error& e) {
    net_logger.trace("bind error, rebind to another address");
    goto bind;
  }
  net_logger.trace("connecting {} from {}", peer, local);
  fd.connect(peer.u.sa, sizeof(peer.u.sas));
  old_conn->attach(fd.get(), local, peer);
}


void posix_connector::addr_pool::generate_ips() {
  struct ifaddrs *ifap, *ifa;
  ::getifaddrs(&ifap);
  for (ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr) {
      continue;
    }
    int family = ifa->ifa_addr->sa_family;
    if (family != AF_INET || strstr(ifa->ifa_name, device.c_str()) == nullptr) {
      continue;
    }

    std::string host = posix_socket::getnameinfo(ifa->ifa_addr);
    ips.push_back(host);
  }
  ::freeifaddrs(ifap);
  net_logger.trace("number of ip addrs: {}", ips.size());
}

void posix_connector::addr_pool::set_device(std::string& dev) {
  device = std::move(dev);
  generate_ips();
}

std::pair<std::string, uint16_t> posix_connector::addr_pool::get() {
  auto now_port = port_index++;
  net_logger.trace("port: {}", port_index);
  if (now_port >= kPortEnd) {
  // (ip,port) tuple is used up on current ip address
    ip_index++;
    if (ip_index >= ips.size()) {
      net_logger.error("IP address running out!");
      engine().stop();
    }
    port_index = kPortStart;
  }
  auto now_ip = ips[ip_index];
  return std::make_pair(now_ip, now_port);
}

socket_address posix_connector::fetch_address() {
  auto addr_pair = addrs_.get();
  return make_ipv4_address(ipv4_addr(addr_pair.first, addr_pair.second));
}

}  // namespace infgen
