#pragma once
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <utility>
#include <string>

namespace infgen {

class buffer {
 public:
  buffer() : buf_(NULL), b_(0), e_(0), cap_(0), exp_(2048) {}
  ~buffer() { delete[] buf_; }
  void clear() {
    delete[] buf_;
    buf_ = nullptr;
    cap_ = 0;
    b_ = e_ = 0;
  }
  size_t size() const { return e_ - b_; }
  bool empty() const { return e_ == b_; }
  char *data() const { return buf_ + b_; }
  char *begin() const { return buf_ + b_; }
  char *end() const { return buf_ + e_; }
  char *make_room(size_t len) {
    if (e_ + len <= cap_) {
    } else if (size() + len < cap_ / 2) {
      move_head();
    } else {
      expand(len);
    }
    return end();
  }

  void make_room() {
    if (space() < exp_) expand(0);
  }

  size_t space() const { return cap_ - e_; }
  void add_size(size_t len) { e_ += len; }

  char *alloc_room(size_t len) {
    char *p = make_room(len);
    add_size(len);
    return p;
  }

  buffer &append(const char *p, size_t len) {
    std::memcpy(alloc_room(len), p, len);
    return *this;
  }

  buffer &append(std::string slice) {
    return append(slice.data(), slice.size());
  }
  buffer &append(const char *p) { return append(p, strlen(p)); }

  template <class T>
  buffer &append_value(const T &v) {
    append((const char *)&v, sizeof v);
    return *this;
  }

  buffer &consume(size_t len) {
    b_ += len;
    if (size() <= 0) clear();
    return *this;
  }


  void set_suggest_size(size_t sz) { exp_ = sz; }
  buffer(const buffer &b) { copy_from(b); }

  buffer &operator=(const buffer &b) {
    if (this == &b) return *this;
    delete[] buf_;
    buf_ = NULL;
    copy_from(b);
    return *this;
  }

  std::string string() { return std::string(data(), size()); }

 private:
  char *buf_;
  size_t b_, e_, cap_, exp_;
  void move_head() {
    std::copy(begin(), end(), buf_);
    e_ -= b_;
    b_ = 0;
  }
  void expand(size_t len) {
    size_t ncap = std::max(exp_, std::max(2 * cap_, size() + len));
    char *p = new char[ncap];
    std::copy(begin(), end(), p);
    e_ -= b_;
    b_ = 0;
    delete[] buf_;
    buf_ = p;
    cap_ = ncap;
  }

  void copy_from(const buffer &b) {
    std::memcpy(this, &b, sizeof b);
    if (b.buf_) {
      buf_ = new char[cap_];
      std::memcpy(data(), b.begin(), b.size());
    }
  }
};

}  // namespace infgen
