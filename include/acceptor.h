#pragma once
#include "inet_addr.h"
#include <functional>

namespace infgen {

class tcp_connection;

using connptr = std::shared_ptr<tcp_connection>;
using tcp_callback = std::function<void(const connptr&)>;

class acceptor {
public:
  ~acceptor() {}
  connptr when_accept(const std::function<connptr()>& cb) { createcb_ = cb; }
  void when_conn_read(tcp_callback&& func) { readcb_ = func; }
  bool bind(socket_address sa);

protected:
  connptr accept();

  std::function<connptr()> createcb_;
  tcp_callback readcb_;

  short local_port_;
  socket_address local_addr_;
  static const int listen_queue_;

  pollable_fd listen_fd_;
};
