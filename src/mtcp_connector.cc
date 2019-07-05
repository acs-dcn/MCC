#include "log.h"
#include "mtcp_connection.h"
#include "mtcp_connector.h"

namespace infgen {

extern logger net_logger;

connptr mtcp_connector::connect(socket_address sa, socket_address lo) {
  mtcp_socket sock = mtcp_socket::socket(sa.u.in.sin_family, SOCK_STREAM, 0);
  sock.set_nonblock();
  sock.connect(sa.u.sa, sizeof(sa.u.sas));

  sockaddr_in local_sa;
  sock.getsockname(sock.get(), (sockaddr*)&local_sa);
  auto local = socket_address(local_sa);

  net_logger.trace("{} - {} connecting...", local, sa);

  auto con = std::make_shared<mtcp_connection>();
  con->attach(sock.get(), local, sa);
  return con;
}

void mtcp_connector::configure(boost::program_options::variables_map vm) {
  int ip_range = vm["ips"].as<int>();
  std::string server_ip = vm["dest"].as<std::string>();
  net_logger.trace("create address pool for ip {}", server_ip);
  auto sa = make_ipv4_address(ipv4_addr(server_ip, 80));
  generate_addrs(ip_range, sa);
}

void mtcp_connector::generate_addrs(int range, socket_address addr) {
  in_port_t dport = addr.u.in.sin_port;
  in_addr_t daddr = addr.u.in.sin_addr.s_addr;
  in_addr_t saddr = INADDR_ANY;

  mtcp_init_rss(engine().context(), saddr, range, daddr, dport);
}

void mtcp_connector::reconnect(connptr old_conn) {
  socket_address sa = old_conn->get_peer();

  mtcp_socket sock = mtcp_socket::socket(sa.u.in.sin_family, SOCK_STREAM, 0);
  sock.set_nonblock();
  sock.connect(sa.u.sa, sizeof(sa.u.sas));

  sockaddr_in local_sa;
  sock.getsockname(sock.get(), (sockaddr*)&local_sa);
  auto local = socket_address(local_sa);

  net_logger.trace("{} - {} connecting...", local, sa);

  old_conn->attach(sock.get(), local, sa);
}

} // namespace infgen
