#pragma once
//#include "codec.h"
#include "posix_connection.h"
#include "pollfd.h"

namespace infgen {

class tcp_server;
using svrptr = std::shared_ptr<tcp_server>;


class tcp_server {
 public:
  tcp_server();
  ~tcp_server();
  bool bind(const socket_address& sa, bool reuse_port = false);
  static svrptr create_tcp_server(const socket_address& sa,
                                  bool reuse_port = false);
  void when_arrived(const std::function<connptr()>& cb) { createcb_ = cb; }
  void when_ready(const connfunc& cb) { readycb_ = cb; }
  void when_failed(const connfunc& cb) { failedcb_ = cb; }
  void when_disconnect(const connfunc& cb) { disconnect_cb_ = cb; }
  void when_recved(const connfunc& cb) {
    readcb_ = cb;
    //  assert(!msgcb_);
  }

  void on_message(const msg_callback& cb) { msgcb_ = cb; }


 private:
  socket_address local_;
  std::shared_ptr<pollable_fd> listen_fd_;
  int listen_queue_;
  std::function<connptr()> createcb_;
  connfunc readycb_, failedcb_, readcb_, disconnect_cb_;
  msg_callback msgcb_;

  void accept();
};
}  // namespace infgen
