export module tracker:response;

import std;
import bencode;
import peer_wire;
import :types;

namespace byte_torrent::tracker {

// ============================================================================
// Compact Peer List Parsing (BEP-23)
// ============================================================================

// Compact format: 6 bytes per peer (4 bytes IP + 2 bytes port)
export [[nodiscard]] std::vector<peer_wire::PeerEndpoint> ParseCompactPeers(
    std::span<std::byte const> data) {
  constexpr std::size_t kPeerSize{6};

  if (data.size() % kPeerSize != 0) {
    throw std::invalid_argument{
        std::format("Invalid compact peer data size: {} (must be multiple of 6)",
                    data.size())};
  }

  std::vector<peer_wire::PeerEndpoint> peers;
  peers.reserve(data.size() / kPeerSize);

  for (std::size_t i = 0; i < data.size(); i += kPeerSize) {
    // IP address (4 bytes, network byte order)
    std::string ip = std::format("{}.{}.{}.{}",
                                 std::to_integer<unsigned>(data[i]),
                                 std::to_integer<unsigned>(data[i + 1]),
                                 std::to_integer<unsigned>(data[i + 2]),
                                 std::to_integer<unsigned>(data[i + 3]));

    // Port (2 bytes, big-endian)
    auto const port = static_cast<std::uint16_t>(
        (std::to_integer<unsigned>(data[i + 4]) << 8) |
        std::to_integer<unsigned>(data[i + 5]));

    peers.push_back({.ip = std::move(ip), .port = port});
  }

  return peers;
}

// Compact IPv6 format: 18 bytes per peer (16 bytes IP + 2 bytes port)
export [[nodiscard]] std::vector<peer_wire::PeerEndpoint> ParseCompactPeers6(
    std::span<std::byte const> data) {
  constexpr std::size_t kPeerSize{18};

  if (data.size() % kPeerSize != 0) {
    throw std::invalid_argument{
        std::format("Invalid compact IPv6 peer data size: {} (must be multiple of 18)",
                    data.size())};
  }

  std::vector<peer_wire::PeerEndpoint> peers;
  peers.reserve(data.size() / kPeerSize);

  for (std::size_t i = 0; i < data.size(); i += kPeerSize) {
    // IPv6 address (16 bytes)
    std::array<std::uint16_t, 8> groups{};
    for (std::size_t j = 0; j < 8; ++j) {
      groups[j] = static_cast<std::uint16_t>(
          (std::to_integer<unsigned>(data[i + j * 2]) << 8) |
          std::to_integer<unsigned>(data[i + j * 2 + 1]));
    }

    std::string ip = std::format("[{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}]",
                                 groups[0], groups[1], groups[2], groups[3],
                                 groups[4], groups[5], groups[6], groups[7]);

    // Port (2 bytes, big-endian)
    auto const port = static_cast<std::uint16_t>(
        (std::to_integer<unsigned>(data[i + 16]) << 8) |
        std::to_integer<unsigned>(data[i + 17]));

    peers.push_back({.ip = std::move(ip), .port = port});
  }

  return peers;
}

// ============================================================================
// Dictionary Peer List Parsing (non-compact format)
// ============================================================================

namespace detail {

[[nodiscard]] peer_wire::PeerEndpoint ParsePeerDict(
    bencode::Dictionary const& peer_dict) {
  peer_wire::PeerEndpoint endpoint{};

  // Required: ip
  if (auto const it = peer_dict.find("ip"); it != peer_dict.end()) {
    if (std::holds_alternative<bencode::ByteString>(it->second)) {
      endpoint.ip = bencode::ToString(std::get<bencode::ByteString>(it->second));
    } else {
      throw std::invalid_argument{"Peer 'ip' must be a string"};
    }
  } else {
    throw std::invalid_argument{"Peer missing 'ip' field"};
  }

  // Required: port
  if (auto const it = peer_dict.find("port"); it != peer_dict.end()) {
    if (std::holds_alternative<bencode::Integer>(it->second)) {
      auto const port_val = std::get<bencode::Integer>(it->second);
      if (port_val < 0 || port_val > 65535) {
        throw std::invalid_argument{
            std::format("Invalid port value: {}", port_val)};
      }
      endpoint.port = static_cast<std::uint16_t>(port_val);
    } else {
      throw std::invalid_argument{"Peer 'port' must be an integer"};
    }
  } else {
    throw std::invalid_argument{"Peer missing 'port' field"};
  }

  return endpoint;
}

[[nodiscard]] std::vector<peer_wire::PeerEndpoint> ParsePeerList(
    bencode::List const& peer_list) {
  std::vector<peer_wire::PeerEndpoint> peers;
  peers.reserve(peer_list.size());

  for (auto const& peer_value : peer_list) {
    if (!std::holds_alternative<bencode::Dictionary>(peer_value)) {
      throw std::invalid_argument{"Peer entry must be a dictionary"};
    }
    peers.push_back(ParsePeerDict(std::get<bencode::Dictionary>(peer_value)));
  }

  return peers;
}

template <typename T>
[[nodiscard]] std::optional<T> GetOptionalInteger(
    bencode::Dictionary const& dict, std::string_view key) {
  auto const it = dict.find(std::string{key});
  if (it == dict.end()) {
    return std::nullopt;
  }
  if (!std::holds_alternative<bencode::Integer>(it->second)) {
    return std::nullopt;
  }
  return static_cast<T>(std::get<bencode::Integer>(it->second));
}

[[nodiscard]] std::optional<std::string> GetOptionalString(
    bencode::Dictionary const& dict, std::string_view key) {
  auto const it = dict.find(std::string{key});
  if (it == dict.end()) {
    return std::nullopt;
  }
  if (!std::holds_alternative<bencode::ByteString>(it->second)) {
    return std::nullopt;
  }
  return bencode::ToString(std::get<bencode::ByteString>(it->second));
}

}  // namespace detail

// ============================================================================
// Response Parsing
// ============================================================================

export [[nodiscard]] TrackerResult<AnnounceResponse> ParseAnnounceResponse(
    std::span<std::byte const> data) {
  bencode::Value root{};
  try {
    root = bencode::DecodeFromSpan(
        std::span{reinterpret_cast<char const*>(data.data()), data.size()});
  } catch (std::exception const& e) {
    return MakeError<AnnounceResponse>(TrackerErrorCode::ParseError,
                                        std::format("Bencode parse error: {}", e.what()));
  }

  if (!std::holds_alternative<bencode::Dictionary>(root)) {
    return MakeError<AnnounceResponse>(TrackerErrorCode::InvalidResponse,
                                        "Response is not a dictionary");
  }

  auto const& dict = std::get<bencode::Dictionary>(root);

  // Check for failure reason
  if (auto const failure = detail::GetOptionalString(dict, "failure reason")) {
    return MakeError<AnnounceResponse>(TrackerErrorCode::TrackerError, *failure);
  }

  AnnounceResponse response{};

  // Required: interval
  if (auto const interval = detail::GetOptionalInteger<std::int64_t>(dict, "interval")) {
    response.interval = std::chrono::seconds{*interval};
  } else {
    return MakeError<AnnounceResponse>(TrackerErrorCode::InvalidResponse,
                                        "Missing 'interval' field");
  }

  // Optional fields
  if (auto const min_interval =
          detail::GetOptionalInteger<std::int64_t>(dict, "min interval")) {
    response.min_interval = std::chrono::seconds{*min_interval};
  }

  response.tracker_id = detail::GetOptionalString(dict, "tracker id");
  response.warning_message = detail::GetOptionalString(dict, "warning message");

  response.complete =
      detail::GetOptionalInteger<std::uint32_t>(dict, "complete").value_or(0);
  response.incomplete =
      detail::GetOptionalInteger<std::uint32_t>(dict, "incomplete").value_or(0);

  // Parse peers
  auto const peers_it = dict.find("peers");
  if (peers_it != dict.end()) {
    if (std::holds_alternative<bencode::ByteString>(peers_it->second)) {
      // Compact format
      auto const& compact_data = std::get<bencode::ByteString>(peers_it->second);
      try {
        response.peers = ParseCompactPeers(
            std::span{compact_data.data(), compact_data.size()});
      } catch (std::exception const& e) {
        return MakeError<AnnounceResponse>(
            TrackerErrorCode::ParseError,
            std::format("Failed to parse compact peers: {}", e.what()));
      }
    } else if (std::holds_alternative<bencode::List>(peers_it->second)) {
      // Dictionary format
      try {
        response.peers =
            detail::ParsePeerList(std::get<bencode::List>(peers_it->second));
      } catch (std::exception const& e) {
        return MakeError<AnnounceResponse>(
            TrackerErrorCode::ParseError,
            std::format("Failed to parse peer list: {}", e.what()));
      }
    }
  }

  // Parse IPv6 peers if present
  auto const peers6_it = dict.find("peers6");
  if (peers6_it != dict.end() &&
      std::holds_alternative<bencode::ByteString>(peers6_it->second)) {
    auto const& compact_data = std::get<bencode::ByteString>(peers6_it->second);
    try {
      auto peers6 = ParseCompactPeers6(
          std::span{compact_data.data(), compact_data.size()});
      response.peers.insert(response.peers.end(),
                            std::make_move_iterator(peers6.begin()),
                            std::make_move_iterator(peers6.end()));
    } catch (...) {
      // IPv6 peers are optional, ignore errors
    }
  }

  return MakeSuccess(std::move(response));
}

// Convenience overload for string_view
export [[nodiscard]] TrackerResult<AnnounceResponse> ParseAnnounceResponse(
    std::string_view data) {
  return ParseAnnounceResponse(
      std::span{reinterpret_cast<std::byte const*>(data.data()), data.size()});
}

// ============================================================================
// Scrape Response Parsing
// ============================================================================

export [[nodiscard]] TrackerResult<ScrapeResponse> ParseScrapeResponse(
    std::span<std::byte const> data) {
  bencode::Value root{};
  try {
    root = bencode::DecodeFromSpan(
        std::span{reinterpret_cast<char const*>(data.data()), data.size()});
  } catch (std::exception const& e) {
    return MakeError<ScrapeResponse>(TrackerErrorCode::ParseError,
                                      std::format("Bencode parse error: {}", e.what()));
  }

  if (!std::holds_alternative<bencode::Dictionary>(root)) {
    return MakeError<ScrapeResponse>(TrackerErrorCode::InvalidResponse,
                                      "Response is not a dictionary");
  }

  auto const& dict = std::get<bencode::Dictionary>(root);

  // Check for failure reason
  if (auto const failure = detail::GetOptionalString(dict, "failure reason")) {
    return MakeError<ScrapeResponse>(TrackerErrorCode::TrackerError, *failure);
  }

  ScrapeResponse response{};

  auto const files_it = dict.find("files");
  if (files_it == dict.end() ||
      !std::holds_alternative<bencode::Dictionary>(files_it->second)) {
    return MakeError<ScrapeResponse>(TrackerErrorCode::InvalidResponse,
                                      "Missing or invalid 'files' field");
  }

  auto const& files_dict = std::get<bencode::Dictionary>(files_it->second);

  for (auto const& [hash_str, file_value] : files_dict) {
    if (hash_str.size() != 20) {
      continue;  // Invalid hash, skip
    }

    if (!std::holds_alternative<bencode::Dictionary>(file_value)) {
      continue;
    }

    auto const& file_dict = std::get<bencode::Dictionary>(file_value);

    std::array<std::byte, 20> info_hash{};
    std::memcpy(info_hash.data(), hash_str.data(), 20);

    ScrapeFile file{};
    file.complete =
        detail::GetOptionalInteger<std::uint32_t>(file_dict, "complete").value_or(0);
    file.incomplete =
        detail::GetOptionalInteger<std::uint32_t>(file_dict, "incomplete").value_or(0);
    file.downloaded =
        detail::GetOptionalInteger<std::uint32_t>(file_dict, "downloaded").value_or(0);
    file.name = detail::GetOptionalString(file_dict, "name");

    response.files[info_hash] = std::move(file);
  }

  return MakeSuccess(std::move(response));
}

}  // namespace byte_torrent::tracker
