#include "log.h"
#include "posix_connection.h"
#include "tcp_server.h"

namespace infgen {
extern logger net_logger;
tcp_server::tcp_server()
    : listen_fd_(nullptr),
      listen_queue_(1024),
      createcb_([] { return connptr(new posix_connection); }) {}

tcp_server::~tcp_server() {
  net_logger.info("server destroyed");
}

bool tcp_server::bind(const socket_address& sa, bool reuse_addr) {
  file_desc fd;
  try {
    fd = file_desc::socket(sa.u.in.sin_family,
                           SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    posix_socket::set_reuseport(fd.get());
    fd.bind(sa.u.sa, sizeof(sa.u.sas));
    fd.listen(listen_queue_);

    local_ = sa;
    ipv4_addr addr(sa);
    net_logger.trace("server listening on {}", addr);

    listen_fd_ = std::make_shared<pollable_fd>(fd);
    listen_fd_->when_readable([=] {
      listen_fd_->enable_read();
      accept();
    });
    listen_fd_->attach_to_loop();

  } catch (std::system_error& e) {
    net_logger.error("fd {} error: {}", fd.get(), e.what());
    return false;
  }
  return true;
}

svrptr tcp_server::create_tcp_server(const socket_address& sa,
                                     bool reuse_port) {
  svrptr p(new tcp_server());
  if (p->bind(sa, reuse_port)) {
    return p;
  } else {
    return nullptr;
  }
}

void tcp_server::accept() {
  file_desc lfd = listen_fd_->get_file_desc();
  try {
    socklen_t len = sizeof(local_.u.sas);
    file_desc cfd = lfd.accept(local_.u.sa, &len);

    sockaddr_in peer, local;
    cfd.getpeername((sockaddr*)&peer);
    cfd.getsockname((sockaddr*)&local);

    auto local_sock = socket_address(local);
    auto peer_sock = socket_address(peer);

    ipv4_addr addr(peer_sock);
    net_logger.trace("connection from {} fd {} accept", addr, cfd.get());
    connptr con = createcb_();
    con->attach(cfd.get(), local_sock, peer_sock);
    if (readycb_) {
      con->when_ready(readycb_);
    }
    if (failedcb_) {
      con->when_ready(failedcb_);
    }
    if (disconnect_cb_) {
      con->when_disconnect(disconnect_cb_);
    }
    if (readcb_) {
      con->when_recved(readcb_);
    }
    if (msgcb_) {
      con->on_message(msgcb_);
    }
  } catch (std::system_error& e) {
    net_logger.error("accept error: {}", e.what());
  }

}
}  // namespace infgen
