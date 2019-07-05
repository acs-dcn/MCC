#pragma once
#include "buffer.h"
#include <memory>
#include <string>

namespace infgen {

class data_source_impl {
public:
  virtual ~data_source_impl();
  virtual temporary_buffer<char> get() = 0;
  virtual temporary_buffer<char> skip(uint64_t n);
  virtual void close();
};

class data_source {
  std::unique_ptr<data_source_impl> dsi_;
public:
  data_source() = default;
  explicit data_source(std::unique_ptr<data_source_impl> dsi)
    : dsi_(std::move(dsi)) {}

  data_source(data_source&& x) = default;
};

class data_sink_impl {
public:
  virtual ~data_sink_impl() {}
  virtual temporary_buffer<char> allocate_buffer(size_t size) {
    return temporary_buffer<char>(size);
  }
  virtual void put(buffer<char> buf) = 0;
  virtual void flush() = 0;
  virtual void close() = 0;
};

class data_sink {
  std::unique_ptr<data_sink_impl> dsi_;

public:
  data_sink() = default;
  explicit data_sink(std::unique_ptr<data_sink_impl> dsi)
      : dsi_(std::move(dsi)) {}
  data_sink(data_sink &&x) = default;
  data_sink &operator=(data_sink &&x) = default;

  buffer<char> allocate_buffer(size_t size) {
    return dsi_->allocate_buffer(size);
  }

  void put(buffer<char> data) { dsi_->put(std::move(data)); }
  void flush() { dsi_->flush(); }
  void close() { dsi_->close(); }
};

template <typename CharType> class input_stream final {
  data_source fd_;
public:
  input_stream(data_source fd): fd_(std::move(fd)) {}
};

template <typename CharType> class output_stream final {
  static_assert(sizeof(CharType) == 1, "must buffer stream of bytes");
  data_sink fd_;
  temporary_buffer<CharType> buf_;

  size_t size_ = 0;
  size_t begin_ = 0;
  size_t end_ = 0;

private:
  size_t available() const { return end_ - begin_; }

public:
  using char_type = CharType;
  output_stream() = default;
  output_stream(data_sink fd, size_t size) : fd_(std::move(fd)), size_(size) {}

  size_t write(const char_type *buf, size_t n);
  size_t write(const char_type *buf);
  size_t write(std::basic_string<char_type> &s);
};

template <typename CharType>
size_t output_stream<CharType>::write(const char_type *buf) {
  return write(buf, strlen(buf));
}

template <typename CharType>
size_t output_stream<CharType>::write(const char_type *buf, size_t n) {
  buffer<char> tmp = fd_.allocate_buffer(n);
  std::copy(buf, buf + n, tmp.get_write());
  return fd_.put(std::move(tmp));
}

} // namespace infgen
