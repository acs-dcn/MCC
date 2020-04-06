#include "epoll.h"
#include "log.h"
#include "posix_connection.h"
#include "reactor.h"

namespace infgen {

extern logger net_logger;

uint64_t tcp_connection::nr_conns = 0;

posix_connection::posix_connection() {
  id_ = ++nr_conns;
}

posix_connection::~posix_connection() {
  net_logger.trace("connection {} destroyed", get_id());
  if (state_ != state::closed && state_ != state::disconnect) {
    close();
  }
}

void posix_connection::attach(int fd, socket_address local, socket_address peer) {
  state_ = state::connecting;
  fd_ = fd;
  local_ = local;
  peer_ = peer;
	req_cnt_ = 0;

  assert(pfd_ == nullptr);
  file_desc desc(fd);
  pfd_ = std::make_shared<pollable_fd>(desc);

  net_logger.trace("construction connection: {}->{}", local_, peer_);

  auto con = shared_from_this();
  pfd_->when_writable([=] { con->handle_write(con); });
  pfd_->when_readable([=] { con->handle_read(con); });
  pfd_->attach_to_loop();
}

void posix_connection::close() {
  // close function handles active close, thus pfd should be
  // explicitly set to null to destroy the connection object
  net_logger.trace("closing fd {}", fd_);

  if (state_ == state::closed || state_ == state::disconnect) {
    // already closed somewhere else
    net_logger.info("multiple close op detected! please check your code");
    return;
  }
  state_ = state::closed;
  pfd_->detach_from_loop();
  pfd_->close_fd();
  pfd_ = nullptr;
  if (on_closed_) {
    on_closed_();
  }
}

void posix_connection::handle_write(connptr con) {
  if (state_ == state::connecting) {
    handle_handshake(con);
  } else if (state_ == state::connected){
    send_packet(output_);
  }
}

bool posix_connection::handle_handshake(connptr con) {
  if (state_ != state::connecting) {
    net_logger.error("handshake called when state = {}", (int)state_);
  }
  auto err = pfd_->get_file_desc().getsockopt<int>(SOL_SOCKET, SO_ERROR);
  if (err != 0) {
    con->set_state(state::failed);
    if (on_failed_) {
      on_failed_(con);
    }
    return false;
  } else {
    con->set_state(state::connected);
    if (on_connected_) {
      net_logger.trace("fd {} connected!", fd_);
      on_connected_(con);
    }
    return true;
  }
}

void posix_connection::handle_read(connptr con) {
  if (state_ == state::connecting && handle_handshake(con)) {
    return;
  }
  file_desc fd = pfd_->get_file_desc();
  while (state_ == state::connected) {
    input_.make_room();
    try {
      auto ret = fd.read(input_.end(), input_.space());
      if (ret.has_value()) {
        if (ret.value() == 0) {
          state_ = state::disconnect;
          cleanup(con);
          break;
        // received data
        } else {
          // watch read event on the fd to detect passive close
          pfd_->enable_read();
          auto nread = ret.value();
          net_logger.trace("fd {} read {} bytes", fd.get(), nread);
          stat_.collect(IN, nread);
          input_.add_size(nread);

          if (on_recved_) on_recved_(con);
        }
      // EAGAIN triggered
      } else if (on_msg_ && input_.size()){
        std::string msg = input_.string();
        on_msg_(con, msg);
        //input_.consume(msg.size());
        break;
      } else {
        break;
      }
    } catch (std::system_error& e) {
      net_logger.error("read error on fd {}: {}", fd.get(), e.what());
      cleanup(con);
      break;
    }
  }
}


size_t posix_connection::send(const void *data, size_t len) {
  if (len == 0) {
    return 0;
  }
  size_t nwrite = 0;
  file_desc fd = pfd_->get_file_desc();
  try {
    nwrite = fd.write(data, len);
    if (nwrite > 0) {
      stat_.collect(OUT, nwrite);
      net_logger.trace("fd {} send {} bytes", fd.get(), nwrite);
      if (nwrite < len) {
        net_logger.trace("Failed to send all data");
      }
    } else if (nwrite == 0) {
      net_logger.trace("send buffer full");
      output_.append((const char*)data, len);
      pfd_->enable_write();
    }
  } catch (std::system_error &e) {
    net_logger.trace("send data error: {}", e.what());
    state_ = state::disconnect;
    auto con = shared_from_this();
    cleanup(con);
  }
  return nwrite;
}

bool posix_connection::send_packet(const void *data, std::size_t len) {
  if (state_ != state::connected) {
    net_logger.error("fd {} trying to send packet via broken connection!", fd_);
    return false;
  }
  if (len == 0) {
    return true;
  }
  size_t bytes = send(data, len);
  if (bytes < len) return false;
  pfd_->enable_read();
  return true;
}

bool posix_connection::send_packet(const std::string &data) {
  return send_packet(data.data(), data.size());
}

bool posix_connection::send_packet(const buffer &buf) {
  return send_packet(buf.begin(), buf.size());
}

void posix_connection::reconnect() {
  net_logger.trace("conn {} reconnecting", get_id());
  auto conn = shared_from_this();
  engine().reconnect(conn);
}

void posix_connection::cleanup(connptr con) {
  net_logger.trace("fd {} closed by peer", fd_);

  pfd_->detach_from_loop();
  pfd_->close_fd();
  pfd_ = nullptr;

  // cleanup handles exceptional close from either server or client, thus
  // the connection state must be disconnect

  if (on_disconnect_) {
    on_disconnect_(con);
  }

}

} // namespace infgen
