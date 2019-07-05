#include <algorithm>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>

namespace http {

std::vector<std::string> split(const std::string& str,
                               const std::string& delim) noexcept {
  /*
  std::string::size_type pos, last_pos = 0, length = str.length();
  std::vector<std::string> tokens;

  while (last_pos < length + 1) {
    pos = str.find_first_of(delim, last_pos);
    if (pos == std::string::npos) {
      pos = length;
    }

    if (pos != last_pos) {
      tokens.emplace_back(str.data() + last_pos, pos - last_pos);
    } last_pos = pos + 1;
  }
  */

  return tokens;
}

const static std::string LINE_END = "\r\n";

enum class method {
  GET,
  HEAD,
  POST,
  PUT,
  DELETE,
  TRACE,
  OPTIONS,
  CONNECT,
  PATCH
};

static std::map<std::string, method> method_map{
    {"GET", method::GET},         {"HEAD", method::HEAD},
    {"POST", method::POST},       {"PUT", method::PUT},
    {"DELETE", method::DELETE},   {"TRACE", method::TRACE},
    {"OPTIONS", method::OPTIONS}, {"CONNECT", method::CONNECT},
    {"PATCH", method::PATCH}};
/// Given a map from keys to values, creates a new map from values to keys

template <typename K, typename V>
static std::map<V, K> reverse_map(const std::map<K, V>& m) {
  std::map<V, K> r;
  for (const auto& kv : m) r[kv.second] = kv.first;
  return r;
}

static std::map<method, std::string> method_to_string = reverse_map(method_map);

enum class version { HTTP_1_0, HTTP_1_1, HTTP_2_0 };

static std::map<std::string, version> version_map{
    {"HTTP/1.0", version::HTTP_1_0},
    {"HTTP/1.1", version::HTTP_1_1},
    {"HTTP/2.0", version::HTTP_2_0}};

static std::map<version, std::string> version_to_string =
    reverse_map(version_map);

class header {
 private:
  std::string key_;
  std::string value_;

 public:
  header() = default;
  header(const std::string& key, const std::string& value) noexcept
      : key_(key), value_(value) {}

  header(std::string&& key, std::string&& value) noexcept
      : key_(std::move(key)), value_(std::move(value)) {}

  header(const header& h): key_(h.key_), value_(h.value_) {}

  header(const std::string& str) {
    std::stringstream header_stream(str);
    std::getline(header_stream, key_, ':');
    std::getline(header_stream, value_);
    value_.erase(
        std::remove_if(value_.begin(), value_.end(),
                       [](unsigned char c) { return std::isspace(c); }),
        value_.end());
  }

  void set_value(const std::string& value)  { value_ = value; }
  const std::string& get_key() const  { return key_; }
  const std::string& get_value() const  { return value_; }

  std::string serialize() const noexcept {
    std::stringstream header_stream;
    header_stream << key_ << ": " << value_ << LINE_END;
    return header_stream.str();
  }
};

class request {
 private:
  version version_;
  method method_;
  std::string resource_;
  std::map<std::string, header> headers_;

 public:
  request(method m, const std::string& resource,
          const std::map<std::string, header>& headers,
          version v=version::HTTP_1_1) noexcept
      : version_(v), method_(m), resource_(resource), headers_(headers) {}

  std::string serialize() const noexcept {
    std::stringstream req_stream;
    req_stream << method_to_string[method_] << " " << resource_ << " "
               << version_to_string[version_] << LINE_END;

    for (const std::pair<const std::string, header>& h : headers_) {
      req_stream << h.second.serialize();
    }

    req_stream << LINE_END;
    return req_stream.str();
  }

  request(const std::string& req_str) {
    std::vector<std::string> lines = split(req_str, LINE_END);

    if (lines.size() < 1) {
      throw std::runtime_error(
          "HTTP Request ('" + std::string(req_str) + "') consisted of " +
          std::to_string(lines.size()) + " lines, should be >= 1.");
    }

    std::vector<std::string> segments = split(lines[0], " ");

    if (segments.size() != 3) {
      throw std::runtime_error("First line of HTTP request ('" +
                               std::string(req_str) + "') consisted of " +
                               std::to_string(segments.size()) +
                               " space separated segments, should be 3.");
    }

    method_ = method_map[segments[0]];
    resource_ = segments[1];
    version_ = version_map[segments[2]];

    for (std::size_t i = 1; i < lines.size(); i++) {
      if (lines[i].size() > 0) {
        header h = header(lines[i]);
        headers_.insert(std::make_pair(h.get_key(), h));
      }
    }
  }
};

class response {
 private:
  int status_code_;
  version version_;
  int content_length_;
  std::map<std::string, header> headers_;

 public:
  constexpr static int OK = 200;
  constexpr static int CREATED = 201;
  constexpr static int ACCEPTED = 202;
  constexpr static int NO_CONTENT = 203;
  constexpr static int BAD_REQUEST = 400;
  constexpr static int FORBIDDEN = 403;
  constexpr static int NOT_FOUND = 404;
  constexpr static int REQUEST_TIMEOUT = 408;
  constexpr static int INTERNAL_SERVER_ERROR = 500;
  constexpr static int BAD_GATEWAY = 502;
  constexpr static int SERVICE_UNAVAILABLE = 503;

  response(int code, version v, const std::map<std::string, header>& headers) noexcept
      : status_code_(code), headers_(headers) {}

  int get_status_code() const noexcept { return status_code_; }
  int get_content_length() const noexcept { return content_length_; }

  const std::map<std::string, header> get_headers() const noexcept { return headers_; }

  response (const std::string& res_str) noexcept {
    std::vector<std::string> header_segments =
        split(res_str, LINE_END);

    const std::string& code_line = header_segments[0];
    std::vector<std::string> code_segments = split(code_line, " ");
    version_ = version_map[code_segments[0]];
    status_code_ = std::stoi(code_segments[1]);

    header_segments.erase(header_segments.begin());
    for (const std::string& line : header_segments) {
      const header h = header(line);
      if (h.get_key() == "Content-Length") {
        content_length_ = std::stoi(h.get_value());
      }
      headers_.insert(std::make_pair(h.get_key(), h));
    }
  }
};

}  // namespace http
