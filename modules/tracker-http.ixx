export module tracker:http;

import std;
import :types;

namespace byte_torrent::tracker {

// ============================================================================
// HTTP Response
// ============================================================================

export struct HttpResponse {
  int status_code{0};
  std::map<std::string, std::string, std::less<>> headers{};
  std::vector<std::byte> body{};

  [[nodiscard]] bool IsSuccess() const noexcept {
    return status_code >= 200 && status_code < 300;
  }

  [[nodiscard]] bool IsRedirect() const noexcept {
    return status_code >= 300 && status_code < 400;
  }

  [[nodiscard]] std::optional<std::string_view> GetHeader(
      std::string_view name) const {
    auto const it = headers.find(name);
    if (it == headers.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  [[nodiscard]] std::string_view BodyAsString() const noexcept {
    return {reinterpret_cast<char const*>(body.data()), body.size()};
  }
};

// ============================================================================
// HTTP Client Interface
// ============================================================================

export class IHttpClient {
 public:
  virtual ~IHttpClient() = default;

  // Perform HTTP GET request
  [[nodiscard]] virtual TrackerResult<HttpResponse> Get(
      Url const& url,
      std::chrono::seconds timeout = std::chrono::seconds{30}) = 0;
};

// ============================================================================
// HTTP Request Builder (for creating raw HTTP/1.1 requests)
// ============================================================================

export [[nodiscard]] inline std::string BuildHttpRequest(
    Url const& url,
    std::string_view method = "GET") {
  std::string request;
  request.reserve(256);

  // Request line
  request += method;
  request += ' ';
  request += url.PathWithQuery();
  request += " HTTP/1.1\r\n";

  // Headers
  request += "Host: ";
  request += url.HostWithPort();
  request += "\r\n";

  request += "User-Agent: ByteTorrent/0.1\r\n";
  request += "Accept: */*\r\n";
  request += "Connection: close\r\n";

  // End of headers
  request += "\r\n";

  return request;
}

// ============================================================================
// HTTP Response Parser
// ============================================================================

export class HttpResponseParser {
 public:
  enum class State { StatusLine, Headers, Body, Complete, Error };

  // Feed data to parser, returns bytes consumed
  std::size_t Feed(std::span<std::byte const> data) {
    std::size_t consumed{0};

    while (consumed < data.size() && 
           state_ != State::Complete &&
           state_ != State::Error) {
      switch (state_) {
        case State::StatusLine:
          consumed += ParseStatusLine(data.subspan(consumed));
          break;
        case State::Headers:
          consumed += ParseHeaders(data.subspan(consumed));
          break;
        case State::Body:
          consumed += ParseBody(data.subspan(consumed));
          break;
        default:
          break;
      }
    }

    return consumed;
  }

  [[nodiscard]] State GetState() const noexcept { return state_; }

  [[nodiscard]] bool IsComplete() const noexcept {
    return state_ == State::Complete;
  }

  [[nodiscard]] bool HasError() const noexcept { return state_ == State::Error; }

  [[nodiscard]] HttpResponse TakeResponse() { return std::move(response_); }

  [[nodiscard]] std::string const& GetError() const noexcept { return error_; }

 private:
  std::size_t ParseStatusLine(std::span<std::byte const> data) {
    // Append to buffer
    for (std::byte const b : data) {
      char const c{static_cast<char>(b)};
      line_buffer_ += c;

      if (c == '\n') {
        // Parse status line: "HTTP/1.1 200 OK\r\n"
        if (line_buffer_.size() < 12) {
          SetError("Invalid status line");
          return line_buffer_.size();
        }

        // Find status code
        auto const space1 = line_buffer_.find(' ');
        if (space1 == std::string::npos) {
          SetError("Invalid status line format");
          return line_buffer_.size();
        }

        auto const space2 = line_buffer_.find(' ', space1 + 1);
        auto const code_str = line_buffer_.substr(
            space1 + 1, (space2 != std::string::npos ? space2 : line_buffer_.size()) -
                            space1 - 1);

        auto [ptr, ec] = std::from_chars(
            code_str.data(), code_str.data() + code_str.size(), response_.status_code);

        if (ec != std::errc{}) {
          SetError("Invalid status code");
          return line_buffer_.size();
        }

        std::size_t const consumed{line_buffer_.size()};
        line_buffer_.clear();
        state_ = State::Headers;
        return consumed;
      }
    }
    return data.size();
  }

  std::size_t ParseHeaders(std::span<std::byte const> data) {
    for (std::size_t i = 0; i < data.size(); ++i) {
      char const c{static_cast<char>(data[i])};
      line_buffer_ += c;

      if (c == '\n') {
        // Check for end of headers (empty line)
        if (line_buffer_ == "\r\n" || line_buffer_ == "\n") {
          line_buffer_.clear();

          // Determine body length
          if (auto const cl = response_.GetHeader("Content-Length")) {
            std::from_chars(cl->data(), cl->data() + cl->size(), content_length_);
          }

          // Check for chunked encoding
          if (auto const te = response_.GetHeader("Transfer-Encoding")) {
            chunked_ = (te->find("chunked") != std::string_view::npos);
          }

          state_ = State::Body;
          return i + 1;
        }

        // Parse header line
        auto const colon = line_buffer_.find(':');
        if (colon != std::string::npos) {
          std::string name{line_buffer_.substr(0, colon)};
          std::string value{line_buffer_.substr(colon + 1)};

          // Trim whitespace
          while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.erase(0, 1);
          }
          while (!value.empty() &&
                 (value.back() == '\r' || value.back() == '\n')) {
            value.pop_back();
          }

          response_.headers[std::move(name)] = std::move(value);
        }

        line_buffer_.clear();
      }
    }
    return data.size();
  }

  std::size_t ParseBody(std::span<std::byte const> data) {
    if (chunked_) {
      return ParseChunkedBody(data);
    }

    // Content-Length based
    std::size_t const remaining{content_length_ - response_.body.size()};
    std::size_t const to_read{std::min(remaining, data.size())};

    response_.body.insert(response_.body.end(), data.begin(),
                          data.begin() + static_cast<std::ptrdiff_t>(to_read));

    if (response_.body.size() >= content_length_) {
      state_ = State::Complete;
    }

    return to_read;
  }

  std::size_t ParseChunkedBody(std::span<std::byte const> data) {
    std::size_t consumed{0};

    while (consumed < data.size()) {
      if (reading_chunk_size_) {
        // Read chunk size line
        char const c{static_cast<char>(data[consumed++])};
        line_buffer_ += c;

        if (c == '\n') {
          // Parse hex chunk size
          std::size_t chunk_size{0};
          auto [ptr, ec] = std::from_chars(
              line_buffer_.data(),
              line_buffer_.data() + line_buffer_.find_first_of("\r\n;"),
              chunk_size, 16);

          if (ec != std::errc{}) {
            SetError("Invalid chunk size");
            return consumed;
          }

          line_buffer_.clear();
          current_chunk_size_ = chunk_size;
          current_chunk_read_ = 0;
          reading_chunk_size_ = false;

          if (chunk_size == 0) {
            state_ = State::Complete;
            return consumed;
          }
        }
      } 
      else {
        // Read chunk data
        std::size_t const remaining{current_chunk_size_ - current_chunk_read_};
        std::size_t const available{data.size() - consumed};
        std::size_t const to_read{std::min(remaining, available)};

        response_.body.insert(
            response_.body.end(),
            data.begin() + static_cast<std::ptrdiff_t>(consumed),
            data.begin() + static_cast<std::ptrdiff_t>(consumed + to_read));

        current_chunk_read_ += to_read;
        consumed += to_read;

        if (current_chunk_read_ >= current_chunk_size_) {
          // Skip trailing CRLF
          reading_chunk_size_ = true;
          skip_crlf_ = true;
        }
      }

      // Skip CRLF after chunk data
      if (skip_crlf_ && consumed < data.size()) {
        char const c{static_cast<char>(data[consumed])};
        if (c == '\r' || c == '\n') {
          ++consumed;
          if (static_cast<char>(data[consumed]) == '\n') {
            ++consumed;
          }
          skip_crlf_ = false;
        }
      }
    }

    return consumed;
  }

  void SetError(std::string_view msg) {
    state_ = State::Error;
    error_ = msg;
  }

  State state_{State::StatusLine};
  HttpResponse response_{};
  std::string line_buffer_{};
  std::string error_{};
  std::size_t content_length_{0};
  bool chunked_{false};
  std::size_t current_chunk_size_{0};
  std::size_t current_chunk_read_{0};
  bool reading_chunk_size_{true};
  bool skip_crlf_{false};
};

}  // namespace byte_torrent::tracker
