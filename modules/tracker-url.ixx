export module tracker:url;

import std;
import :types;

namespace byte_torrent::tracker {

// ============================================================================
// URL Encoding (RFC 3986)
// ============================================================================

namespace detail {

[[nodiscard]] bool IsUnreserved(char c) noexcept {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' ||
         c == '_' || c == '~';
}

[[nodiscard]] constexpr char ToHexDigit(unsigned value) noexcept {
  return static_cast<char>(value < 10 ? '0' + value : 'A' + value - 10);
}

[[nodiscard]] constexpr int FromHexDigit(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

}  // namespace detail

// URL-encode a byte span (for info_hash, peer_id)
export [[nodiscard]] std::string UrlEncode(std::span<std::byte const> data) {
  std::string result;
  result.reserve(data.size() * 3);  // Worst case: all bytes encoded

  for (std::byte const b : data) {
    char const c{static_cast<char>(b)};
    if (detail::IsUnreserved(c)) {
      result += c;
    } else {
      result += '%';
      result += detail::ToHexDigit((static_cast<unsigned char>(c) >> 4) & 0x0F);
      result += detail::ToHexDigit(static_cast<unsigned char>(c) & 0x0F);
    }
  }

  return result;
}

// URL-encode a string
export [[nodiscard]] std::string UrlEncode(std::string_view str) {
  std::string result;
  result.reserve(str.size() * 3);

  for (char const c : str) {
    if (detail::IsUnreserved(c)) {
      result += c;
    } else {
      result += '%';
      result += detail::ToHexDigit((static_cast<unsigned char>(c) >> 4) & 0x0F);
      result += detail::ToHexDigit(static_cast<unsigned char>(c) & 0x0F);
    }
  }

  return result;
}

// URL-decode a string
export [[nodiscard]] std::string UrlDecode(std::string_view str) {
  std::string result;
  result.reserve(str.size());

  for (std::size_t i = 0; i < str.size(); ++i) {
    if (str[i] == '%' && i + 2 < str.size()) {
      int const high = detail::FromHexDigit(str[i + 1]);
      int const low = detail::FromHexDigit(str[i + 2]);
      if (high >= 0 && low >= 0) {
        result += static_cast<char>((high << 4) | low);
        i += 2;
        continue;
      }
    }
    result += str[i];
  }

  return result;
}

// ============================================================================
// URL Parsing
// ============================================================================

export [[nodiscard]] std::optional<Url> ParseUrl(std::string_view url_str) {
  Url url{};

  // Find scheme
  auto const scheme_end = url_str.find("://");
  if (scheme_end == std::string_view::npos) {
    return std::nullopt;
  }
  url.scheme = std::string{url_str.substr(0, scheme_end)};
  url_str = url_str.substr(scheme_end + 3);

  // Set default port based on scheme
  if (url.scheme == "http") {
    url.port = 80;
  } else if (url.scheme == "https") {
    url.port = 443;
  }

  // Find path start
  auto const path_start = url_str.find('/');
  std::string_view authority =
      (path_start == std::string_view::npos) ? url_str : url_str.substr(0, path_start);

  // Parse host and port from authority
  // Handle IPv6 addresses: [::1]:8080
  if (!authority.empty() && authority[0] == '[') {
    // IPv6 address
    auto const bracket_end = authority.find(']');
    if (bracket_end == std::string_view::npos) {
      return std::nullopt;
    }
    url.host = std::string{authority.substr(0, bracket_end + 1)};
    authority = authority.substr(bracket_end + 1);

    // Check for port
    if (!authority.empty() && authority[0] == ':') {
      authority = authority.substr(1);
      std::uint16_t port{};
      auto [ptr, ec] =
          std::from_chars(authority.data(), authority.data() + authority.size(), port);
      if (ec != std::errc{}) {
        return std::nullopt;
      }
      url.port = port;
    }
  } else {
    // Regular hostname or IPv4
    auto const port_sep = authority.find(':');
    if (port_sep == std::string_view::npos) {
      url.host = std::string{authority};
    } else {
      url.host = std::string{authority.substr(0, port_sep)};
      auto const port_str = authority.substr(port_sep + 1);
      std::uint16_t port{};
      auto [ptr, ec] =
          std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
      if (ec != std::errc{}) {
        return std::nullopt;
      }
      url.port = port;
    }
  }

  if (url.host.empty()) {
    return std::nullopt;
  }

  // Parse path and query
  if (path_start != std::string_view::npos) {
    auto remaining = url_str.substr(path_start);
    auto const query_start = remaining.find('?');
    if (query_start == std::string_view::npos) {
      url.path = std::string{remaining};
    } else {
      url.path = std::string{remaining.substr(0, query_start)};
      url.query = std::string{remaining.substr(query_start + 1)};
    }
  }

  if (url.path.empty()) {
    url.path = "/";
  }

  return url;
}

// ============================================================================
// Query String Building
// ============================================================================

export class QueryBuilder {
 public:
  QueryBuilder& Add(std::string_view key, std::string_view value) {
    AppendSeparator();
    query_ += key;
    query_ += '=';
    query_ += UrlEncode(value);
    return *this;
  }

  QueryBuilder& Add(std::string_view key, std::span<std::byte const> value) {
    AppendSeparator();
    query_ += key;
    query_ += '=';
    query_ += UrlEncode(value);
    return *this;
  }

  template <std::integral T>
  QueryBuilder& Add(std::string_view key, T value) {
    AppendSeparator();
    query_ += key;
    query_ += '=';
    query_ += std::to_string(value);
    return *this;
  }

  QueryBuilder& AddIf(bool condition, std::string_view key, std::string_view value) {
    if (condition) {
      Add(key, value);
    }
    return *this;
  }

  template <typename T>
  QueryBuilder& AddOptional(std::string_view key, std::optional<T> const& value) {
    if (value.has_value()) {
      Add(key, *value);
    }
    return *this;
  }

  [[nodiscard]] std::string Build() const { return query_; }

  [[nodiscard]] std::string const& Get() const noexcept { return query_; }

  void Clear() { query_.clear(); }

 private:
  void AppendSeparator() {
    if (!query_.empty()) {
      query_ += '&';
    }
  }

  std::string query_{};
};

// ============================================================================
// Announce URL Building
// ============================================================================

export [[nodiscard]] std::string BuildAnnounceQuery(AnnounceRequest const& request) {
  QueryBuilder builder{};

  builder.Add("info_hash", std::span{request.info_hash})
      .Add("peer_id", std::span<std::byte const>{request.peer_id})
      .Add("port", request.port)
      .Add("uploaded", request.uploaded)
      .Add("downloaded", request.downloaded)
      .Add("left", request.left)
      .Add("compact", request.compact ? 1 : 0);

  // Add event if not None
  if (request.event != TrackerEvent::None) {
    builder.Add("event", ToString(request.event));
  }

  builder.AddOptional("numwant", request.numwant)
      .AddOptional("trackerid", request.tracker_id)
      .AddOptional("ip", request.ip);

  return builder.Build();
}

export [[nodiscard]] Url BuildAnnounceUrl(std::string_view announce_url,
                                          AnnounceRequest const& request) {
  auto url = ParseUrl(announce_url);
  if (!url) {
    throw std::invalid_argument{
        std::format("Invalid announce URL: {}", announce_url)};
  }

  std::string query = BuildAnnounceQuery(request);

  // Merge with existing query string
  if (!url->query.empty()) {
    url->query += '&' + query;
  } else {
    url->query = std::move(query);
  }

  return *url;
}

// ============================================================================
// Scrape URL Building (BEP-48)
// ============================================================================

export [[nodiscard]] std::optional<std::string> GetScrapeUrl(
    std::string_view announce_url) {
  // Replace "announce" with "scrape" in the path
  auto const pos = announce_url.rfind("/announce");
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::string scrape_url{announce_url};
  scrape_url.replace(pos, 9, "/scrape");
  return scrape_url;
}

}  // namespace byte_torrent::tracker
