#pragma once

#include "connection.h"
#include "inet_addr.h"
#include "epoll.h"

namespace infgen {

class posix_connection : public tcp_connection {
public:
  posix_connection();
  ~posix_connection();
  virtual bool send_packet(const void *data, std::size_t len) override;
  virtual bool send_packet(const std::string &data) override;
  virtual bool send_packet(const buffer &buf) override;
  void attach(int fd, socket_address local, socket_address peer);
  void reconnect() override;
  virtual void close() override;
  void handle_write(connptr con) override;
  void handle_read(connptr con) override;

private:
  std::shared_ptr<pollable_fd> pfd_;
  void cleanup(connptr con);
  size_t send(const void *data, size_t len);
  bool handle_handshake(connptr con);
};
} // namespace infgen
