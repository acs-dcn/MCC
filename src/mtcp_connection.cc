#include "log.h"
#include "mepoll.h"
#include "mtcp_connection.h"
#include "reactor.h"

#ifdef AES_GCM
#include "ssl_layer.h"
#endif

namespace infgen {

extern logger net_logger;

mtcp_connection::mtcp_connection() {
  id_ = ++nr_conns;
}

mtcp_connection::~mtcp_connection() {
  net_logger.trace("connection id {}  destroyed", id_);
  if (state_ != state::closed && state_ != state::disconnect) {
    close();
  }
}

void mtcp_connection::attach(int sockid, socket_address local, socket_address peer) {
  state_ = state::connecting;
  fd_ = sockid;
  local_ = local;
  peer_ = peer;

  assert(pfd_ == nullptr);
  mtcp_socket sock(sockid, engine().context());
  pfd_ = std::make_shared<pollable_fd>(sock);

  auto con = shared_from_this();
  pfd_->when_writable([=] { con->handle_write(con);});
  pfd_->when_readable([=] { con->handle_read(con);});
  pfd_->attach_to_loop();
}

void mtcp_connection::close() {
  net_logger.trace("closing mtcp socket {}", fd_);
  if (state_ == state::closed || state_ == state::disconnect) {
    net_logger.trace("multiple close detected! please check your code");
    return;
  }
  state_ = state::closed;
  pfd_->detach_from_loop();
  pfd_->close_socket();
  pfd_ = nullptr;
  if (on_closed_) {
    on_closed_();
  }
}

size_t mtcp_connection::send(const void *data, size_t len) {
  if (len == 0) {
    return 0;
  }
  size_t nwrite = 0;
#ifdef AES_GCM
  try {
		char ciphertext[1500];
		ssl_layer::ssl_encrypt(engine().ssl_context(), (const unsigned char *)data, (unsigned char*)ciphertext, len);
		len += 21; // Additianal + ciphertext + tag
    nwrite = pfd_->get_mtcp_socket().write((const char*)ciphertext, len);
#else
  try {
    nwrite = pfd_->get_mtcp_socket().write((const char *)data, len);
#endif
    if (nwrite > 0) {
      stat_.collect(OUT, nwrite);
      net_logger.trace("Socket {} send {} bytes", pfd_->get_id(), nwrite);
      if (nwrite < len) {
        net_logger.trace("Send buffer full!");
      }
    } else if (nwrite == 0) {
      net_logger.trace("Send buffer full! Unable to send data");
      //output_.append((const char*)data, len);
      //pfd_->enable_write();
    }

  } catch (std::system_error &e) {
    net_logger.error("Send data error: {} Socket id: {}", e.what(), pfd_->get_id());
    state_ = state::disconnect;
  }
  return nwrite;
}

bool mtcp_connection::send_packet(const void *data, std::size_t len) {
  if (state_ != state::connected) {
    net_logger.error("trying to send packet via broken connection!");
    return false;
  }

  if (len == 0) {
    return true;
  }

  size_t bytes = send(data, len);
  if (bytes < len) {
    return false;
  }
  pfd_->enable_read();
  return true;
}

bool mtcp_connection::send_packet(const std::string &data) {
  return send_packet(data.data(), data.size());
}

bool mtcp_connection::send_packet(const buffer &buf) {
  return send_packet(buf.begin(), buf.size());
}

void mtcp_connection::reconnect() {
  net_logger.info("conn {} reconnecting", get_id());
  auto conn = shared_from_this();
  engine().reconnect(conn);
}

void mtcp_connection::handle_write(connptr con) {
  if (state_ == state::connecting) {
    handle_handshake(con);
  } else {
    send_packet(output_);
  }
}

bool mtcp_connection::handle_handshake(connptr con) {
  if (state_ != state::connecting) {
    net_logger.error("handshake called when state = {}", (int)state_);
  }
  auto err = pfd_->get_mtcp_socket().getsockopt<int>(SOL_SOCKET, SO_ERROR);
  if (err != 0) {
    con->set_state(state::failed);
    if (on_failed_) {
      on_failed_(con);
    }
    return false;
  } else {
    con->set_state(state::connected);
    if (on_connected_) {
      net_logger.trace("Socket {} connected!", fd_);
      on_connected_(con);
    }
    return true;
  }
}

void mtcp_connection::handle_read(connptr con) {
  if (state_ == state::connecting && handle_handshake(con)) {
    return;
  }
  mtcp_socket sock = pfd_->get_mtcp_socket();
  net_logger.trace("Socket {} in state {} handle read event",
      sock.get(), (int)get_state());

  while (state_ == state::connected) {
    input_.make_room();
    try {
			std::optional<int> ret; 
#ifdef AES_GCM
			char data_read[1500];
			auto data_len = sock.read(data_read, 1500);
			if (data_len.has_value()) {
					int len_tmp = data_len.value();
					if (len_tmp < 16) {
						net_logger.error("Socket {} in state {}, read data with unexpected length.",
										sock.get(), (int)get_state());
					} 
					ret = ssl_layer::ssl_decrypt(engine().ssl_context(), (const unsigned char*)(data_read + 5), (unsigned char*)input_.end(), len_tmp - 21);
			} else {
			 	ret = std::nullopt;
			}
#else
      ret = sock.read(input_.end(), input_.space());
#endif
      if (ret.has_value()) {
        if (ret.value() == 0) {
          // connection closed by peer
          state_ = state::disconnect;
          cleanup(con);
          break;
        } else {
          pfd_->enable_read();
          auto nread = ret.value();
          net_logger.trace("socket {} read {} bytes", sock.get(), nread);
          stat_.collect(IN, nread);
          input_.add_size(nread);
          if (on_recved_) on_recved_(con);
        }
      } else if (on_msg_ && input_.size()) {
        std::string msg = input_.string();
        on_msg_(con, msg);
        //input_.consume(msg.size());
        break;
      } else {
        net_logger.trace("resource temporarily unavailable");
        // EAGIAN condition, continual watching read
        pfd_->enable_read();
        break;
      }
    } catch (std::system_error& e) {
      net_logger.trace("Read error on socket {}: {}", sock.get(), e.what());
      state_ = state::disconnect;
      cleanup(con);
      break;
    }
  }
}


void mtcp_connection::cleanup(connptr con) {
  net_logger.info("mTCP socket {} closed by peer", pfd_->get_id());
  pfd_->detach_from_loop();
  pfd_->close_socket();
  pfd_ = nullptr;

  if (on_disconnect_) {
    on_disconnect_(con);
  }
}

} // namespace infgen
