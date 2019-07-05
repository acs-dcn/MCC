#pragma once

#include "connection.h"
#include "mepoll.h"
#include "inet_addr.h"

namespace infgen {

class mtcp_connection : public tcp_connection {
public:
  mtcp_connection();
  ~mtcp_connection();

  virtual bool send_packet(const void *data, std::size_t len) override;
  virtual bool send_packet(const std::string &data) override;
  virtual bool send_packet(const buffer &buf) override;

  virtual void close() override;
  void handle_write(connptr con) override;
  void handle_read(connptr con) override;
  void attach(int sock, socket_address local, socket_address peer) override;
  void reconnect() override;

private:
  std::shared_ptr<pollable_fd> pfd_;
  size_t send(const void *data, size_t len);
  void cleanup(connptr con);
  bool handle_handshake(connptr con);
};
} // namespace infgen
