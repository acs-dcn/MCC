#include "log.h"
#include "acceptor.h"
#include "inet_addr.h"

namespace infgen {

extern logger net_logger;

acceptor::listen_queue_ = 1024;

bool acceptor::bind(socket_address sa) {
  file_desc fd = file_desc::socket(
      sa.u.in.sin_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  try {
    fd.bind(sa.u.sa, sizeof(sa.u.sas));
  } catch (std::system_error& e) {
    fd.close();
    net_logger.warn("bind failed, {}", e.what());
    return false;
  }

  local_addr_ = sa;
  local_port_ = ntohs(sa.u.in.sin_port);

  try {
    fd.listen(listen_queue_);
  } catch (std::system_error& e) {
    net_logger.warn("listen failed, {}", e.what());
    return false;
  }

  net_logger.info("listening on port {}", local_port_); 
  listen_fd_ = std::make_shared<pollable_fd>(std::move(fd));
  listen_fd_->when_readable([this] {
    accept();
  }
  listen_fd_->attach_to_loop();
  return true;
}

connptr acceptor::accept() {
  socket_address peer_addr; 
  try {
    file_desc cfd = listen_fd_.get_file_desc().accept(local_addr_.u.sa, sizeof(local_addr_.u.sas));
    cfd.getpeername(&peer_addr.u.sa);
  } catch (std::system_error& e) {
    net_logger.error("{}", e.what());
  }

  auto ready_pfd = std::make_shared<pollable_fd>(std::move(cfd));
  auto conn = std::make_shared<posix_connection>(ready_pfd, local_addr_, peer_addr);

  return conn;
}
  
}

}  // namespace infgen
