#pragma once

#include "buffer.h"
#include "connector.h"
#include "timer.h"

namespace infgen {
using connfunc = std::function<void(const connptr&)>;
using callback_t = std::function<void()>;
using msg_callback = std::function<void(const connptr&, std::string& msg)>;

class tcp_connection : public std::enable_shared_from_this<tcp_connection> {
 public:
  enum class state {
    invalid,
    connecting,
    failed,
    connected,
    disconnect,
    closed
  };

  tcp_connection() = default;
  tcp_connection(const tcp_connection &) = default;
  tcp_connection &operator=(const tcp_connection &) = default;

  virtual ~tcp_connection() {}
  virtual state get_state() { return state_; }
  virtual void set_state(state s) { state_ = s; }
  virtual uint64_t get_id() { return id_; }
  virtual void set_id(uint64_t id) { id_ = id; }
  virtual uint64_t get_fd() { return fd_; }

  virtual bool send_packet(const void *data, std::size_t len) = 0;
  virtual bool send_packet(const std::string &data) = 0;
  virtual bool send_packet(const buffer &buf) = 0;

  template <typename Func>
  void when_recved(Func &&func) {
    on_recved_ = std::forward<Func>(func);
  }

  template <typename Func>
  void on_message(Func &&func) {
    on_msg_ = std::forward<Func>(func);
  }

  template <typename Func>
  void when_ready(Func &&func) {
    on_connected_ = std::forward<Func>(func);
  }

  template <typename Func>
  void when_failed(Func &&func) {
    on_failed_ = std::forward<Func>(func);
  }

  template <typename Func>
  void when_closed(Func &&func) {
    on_closed_ = std::forward<Func>(func);
  }

  template <typename Func>
  void when_disconnect(Func &&func) {
    on_disconnect_ = std::forward<Func>(func);
  }

  virtual void handle_write(connptr con) = 0;
  virtual void handle_read(connptr con) = 0;
  virtual void close() = 0;
  virtual void attach(int fd, socket_address local, socket_address peer) = 0;
  virtual void reconnect() = 0;

  virtual uint64_t tx_bytes() { return stat_.data_in; }
  virtual uint64_t rx_bytes() { return stat_.data_out; }

  virtual buffer& get_input() { return input_; }
  virtual buffer& get_output() { return output_; }

  socket_address get_peer() { return peer_; }
  socket_address get_local() { return local_; }

  static uint64_t nr_conns;
	//@ wuwenqing, for request counter
	uint32_t req_cnt_;

 protected:
  static const int IN = 0;
  static const int OUT = 1;

  struct conn_stat {
    uint64_t data_in;
    uint64_t data_out;
    milliseconds alive_time;
    system_clock::time_point born_tp;
    system_clock::time_point dead_tp;

    void collect(int direction, uint64_t value) {
      if (direction == IN) {
        data_in += value;
      } else if (direction == OUT) {
        data_out += value;
      }
    }
  };

  buffer input_, output_;
  conn_stat stat_;
  connfunc on_connected_, on_failed_, on_recved_, on_disconnect_;
  msg_callback on_msg_;
  callback_t on_closed_;
  state state_;
  uint64_t id_, fd_;

  socket_address local_, peer_;
};

}  // namespace infgen
