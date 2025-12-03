export module tracker:types;

import std;
import peer_wire;

namespace byte_torrent::tracker {

// ============================================================================
// Tracker Event
// ============================================================================

export enum class TrackerEvent {
  None,       // Regular announce (periodic)
  Started,    // First announce when download starts
  Stopped,    // Announce when client shuts down
  Completed   // Announce when download completes (becomes seeder)
};

export [[nodiscard]] constexpr std::string_view ToString(
    TrackerEvent event) noexcept {
  switch (event) {
    case TrackerEvent::None:
      return "";
    case TrackerEvent::Started:
      return "started";
    case TrackerEvent::Stopped:
      return "stopped";
    case TrackerEvent::Completed:
      return "completed";
    default:
      return "";
  }
}

// ============================================================================
// Announce Request
// ============================================================================

export struct AnnounceRequest {
  std::array<std::byte, 20> info_hash{};   // SHA1 hash of info dict
  peer_wire::PeerId peer_id{};             // Our peer ID
  std::uint16_t port{6881};                // Port we're listening on
  std::uint64_t uploaded{0};               // Total bytes uploaded
  std::uint64_t downloaded{0};             // Total bytes downloaded
  std::uint64_t left{0};                   // Bytes remaining to download
  TrackerEvent event{TrackerEvent::None};  // Event type
  bool compact{true};                      // Request compact peer list (BEP-23)
  std::optional<std::uint32_t> numwant{};  // Number of peers wanted
  std::optional<std::string> tracker_id{}; // Tracker ID from previous response
  std::optional<std::string> ip{};         // Optional IP override

  [[nodiscard]] bool IsValid() const noexcept {
    // Info hash and peer_id must not be all zeros
    bool info_hash_valid =
        std::ranges::any_of(info_hash, [](std::byte b) { return b != std::byte{0}; });
    bool peer_id_valid =
        std::ranges::any_of(peer_id, [](std::byte b) { return b != std::byte{0}; });
    return info_hash_valid && peer_id_valid && port > 0;
  }
};

// ============================================================================
// Announce Response
// ============================================================================

export struct AnnounceResponse {
  std::chrono::seconds interval{};                    // Re-announce interval
  std::optional<std::chrono::seconds> min_interval{}; // Minimum re-announce interval
  std::optional<std::string> tracker_id{};            // Tracker session ID
  std::uint32_t complete{0};                          // Number of seeders
  std::uint32_t incomplete{0};                        // Number of leechers
  std::vector<peer_wire::PeerEndpoint> peers{};       // Peer list
  std::optional<std::string> warning_message{};       // Optional warning

  [[nodiscard]] std::size_t PeerCount() const noexcept { return peers.size(); }

  [[nodiscard]] bool HasPeers() const noexcept { return !peers.empty(); }
};

// ============================================================================
// Tracker Error
// ============================================================================

export enum class TrackerErrorCode {
  None,
  NetworkError,
  Timeout,
  InvalidResponse,
  TrackerError,  // Tracker returned failure reason
  HttpError,
  ParseError
};

export [[nodiscard]] constexpr std::string_view ToString(
    TrackerErrorCode code) noexcept {
  switch (code) {
    case TrackerErrorCode::None:
      return "None";
    case TrackerErrorCode::NetworkError:
      return "NetworkError";
    case TrackerErrorCode::Timeout:
      return "Timeout";
    case TrackerErrorCode::InvalidResponse:
      return "InvalidResponse";
    case TrackerErrorCode::TrackerError:
      return "TrackerError";
    case TrackerErrorCode::HttpError:
      return "HttpError";
    case TrackerErrorCode::ParseError:
      return "ParseError";
    default:
      return "Unknown";
  }
}

export struct TrackerError {
  TrackerErrorCode code{TrackerErrorCode::None};
  std::string message{};

  [[nodiscard]] bool IsOk() const noexcept {
    return code == TrackerErrorCode::None;
  }

  [[nodiscard]] explicit operator bool() const noexcept { return !IsOk(); }
};

// Result type for announce operations
export template <typename T>
struct TrackerResult {
  std::optional<T> value{};
  TrackerError error{};

  [[nodiscard]] bool IsOk() const noexcept { return value.has_value(); }
  [[nodiscard]] explicit operator bool() const noexcept { return IsOk(); }

  [[nodiscard]] T& operator*() & { return *value; }
  [[nodiscard]] T const& operator*() const& { return *value; }
  [[nodiscard]] T&& operator*() && { return std::move(*value); }

  [[nodiscard]] T* operator->() { return &*value; }
  [[nodiscard]] T const* operator->() const { return &*value; }
};

export template <typename T>
[[nodiscard]] TrackerResult<T> MakeSuccess(T value) {
  return TrackerResult<T>{.value = std::move(value)};
}

export template <typename T>
[[nodiscard]] TrackerResult<T> MakeError(TrackerErrorCode code,
                                          std::string message = {}) {
  return TrackerResult<T>{.error = {.code = code, .message = std::move(message)}};
}

// ============================================================================
// URL Components
// ============================================================================

export struct Url {
  std::string scheme{};      // "http" or "https"
  std::string host{};        // hostname or IP
  std::uint16_t port{80};    // port number
  std::string path{"/"};     // path component
  std::string query{};       // query string (without '?')

  [[nodiscard]] bool IsValid() const noexcept {
    return !scheme.empty() && !host.empty();
  }

  [[nodiscard]] bool IsHttps() const noexcept { return scheme == "https"; }

  [[nodiscard]] std::string ToString() const {
    std::string result = scheme + "://" + host;

    // Add port if non-default
    bool const default_port =
        (scheme == "http" && port == 80) || (scheme == "https" && port == 443);
    if (!default_port) {
      result += ':' + std::to_string(port);
    }

    result += path;

    if (!query.empty()) {
      result += '?' + query;
    }

    return result;
  }

  [[nodiscard]] std::string HostWithPort() const {
    bool const default_port =
        (scheme == "http" && port == 80) || (scheme == "https" && port == 443);
    if (default_port) {
      return host;
    }
    return host + ':' + std::to_string(port);
  }

  [[nodiscard]] std::string PathWithQuery() const {
    if (query.empty()) {
      return path;
    }
    return path + '?' + query;
  }
};

// ============================================================================
// Scrape Request/Response (BEP-48)
// ============================================================================

export struct ScrapeRequest {
  std::vector<std::array<std::byte, 20>> info_hashes{};
};

export struct ScrapeFile {
  std::uint32_t complete{0};    // Seeders
  std::uint32_t incomplete{0};  // Leechers
  std::uint32_t downloaded{0};  // Completed downloads
  std::optional<std::string> name{};
};

export struct ScrapeResponse {
  std::map<std::array<std::byte, 20>, ScrapeFile> files{};
};

}  // namespace byte_torrent::tracker
