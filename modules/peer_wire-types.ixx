export module peer_wire:types;

import std;

namespace byte_torrent::peer_wire {

// ============================================================================
// Protocol Constants
// ============================================================================

export constexpr std::uint8_t kProtocolNameLength{19};
export constexpr std::string_view kProtocolName{"BitTorrent protocol"};
export constexpr std::size_t kReservedBytesSize{8};
export constexpr std::size_t kInfoHashSize{20};
export constexpr std::size_t kPeerIdSize{20};
export constexpr std::size_t kHandshakeSize{1 + kProtocolNameLength +
                                            kReservedBytesSize + kInfoHashSize +
                                            kPeerIdSize};  // 68 bytes

export constexpr std::size_t kMessageLengthSize{4};
export constexpr std::size_t kMessageIdSize{1};
export constexpr std::size_t kBlockSize{16384};      // 16 KB - standard block
export constexpr std::size_t kMaxBlockSize{32768};   // 32 KB - max allowed
export constexpr std::size_t kMaxMessageSize{1 << 24};  // 16 MB safety limit

export constexpr std::chrono::seconds kKeepAliveInterval{120};
export constexpr std::chrono::seconds kHandshakeTimeout{30};
export constexpr std::chrono::seconds kRequestTimeout{60};

// ============================================================================
// Reserved Bytes (for protocol extensions)
// ============================================================================

export using ReservedBytes = std::array<std::byte, kReservedBytesSize>;

namespace extensions {
// BEP-10: Extension Protocol
export constexpr std::size_t kExtensionProtocolBit{43};  // Bit 43 (byte 5, bit 3)

export [[nodiscard]] constexpr ReservedBytes WithExtensionProtocol() noexcept {
  ReservedBytes reserved{};
  reserved[5] = std::byte{0x10};  // Set bit for extension protocol
  return reserved;
}

export [[nodiscard]] constexpr bool SupportsExtensionProtocol(
    ReservedBytes const& reserved) noexcept {
  return (reserved[5] & std::byte{0x10}) != std::byte{0};
}
}  // namespace extensions

// ============================================================================
// Peer Identification
// ============================================================================

export using PeerId = std::array<std::byte, kPeerIdSize>;

export [[nodiscard]] inline PeerId GeneratePeerId(
    std::string_view client_prefix = "-BT0001-") {
  if (client_prefix.size() > kPeerIdSize) {
    throw std::invalid_argument{"Client prefix exceeds peer ID size"};
  }

  PeerId peer_id{};

  // Copy prefix
  std::size_t const prefix_len{std::min(client_prefix.size(), kPeerIdSize)};
  for (std::size_t i{0}; i < prefix_len; ++i) {
    peer_id[i] = static_cast<std::byte>(client_prefix[i]);
  }

  // Fill remainder with random bytes
  std::random_device rd{};
  std::mt19937 gen{rd()};
  std::uniform_int_distribution<unsigned> dist{0, 255};

  for (std::size_t i{prefix_len}; i < kPeerIdSize; ++i) {
    peer_id[i] = static_cast<std::byte>(dist(gen));
  }

  return peer_id;
}

export [[nodiscard]] inline std::string PeerIdToString(PeerId const& id) {
  std::string result;
  result.reserve(kPeerIdSize);
  for (std::byte const b : id) {
    char const c{static_cast<char>(b)};
    if (std::isprint(static_cast<unsigned char>(c))) {
      result += c;
    } else {
      std::format_to(std::back_inserter(result), "\\x{:02x}",
                     std::to_integer<unsigned>(b));
    }
  }
  return result;
}

// ============================================================================
// Message Types (BEP-3)
// ============================================================================

export enum class MessageId : std::uint8_t {
  Choke = 0,
  Unchoke = 1,
  Interested = 2,
  NotInterested = 3,
  Have = 4,
  Bitfield = 5,
  Request = 6,
  Piece = 7,
  Cancel = 8,
  Port = 9,       // DHT extension (BEP-5)
  Extended = 20,  // Extension protocol (BEP-10)
};

export [[nodiscard]] constexpr std::string_view ToString(MessageId id) noexcept {
  switch (id) {
    case MessageId::Choke:
      return "Choke";
    case MessageId::Unchoke:
      return "Unchoke";
    case MessageId::Interested:
      return "Interested";
    case MessageId::NotInterested:
      return "NotInterested";
    case MessageId::Have:
      return "Have";
    case MessageId::Bitfield:
      return "Bitfield";
    case MessageId::Request:
      return "Request";
    case MessageId::Piece:
      return "Piece";
    case MessageId::Cancel:
      return "Cancel";
    case MessageId::Port:
      return "Port";
    case MessageId::Extended:
      return "Extended";
    default:
      return "Unknown";
  }
}

// ============================================================================
// Block Addressing
// ============================================================================

export struct BlockInfo {
  std::uint32_t piece_index{};
  std::uint32_t offset{};
  std::uint32_t length{kBlockSize};

  [[nodiscard]] friend constexpr auto operator<=>(BlockInfo const&,
                                                  BlockInfo const&) = default;
};

export [[nodiscard]] constexpr bool IsValidBlockLength(
    std::uint32_t length) noexcept {
  return length > 0 && length <= kMaxBlockSize;
}

export [[nodiscard]] constexpr bool IsValidBlockOffset(
    std::uint32_t offset, std::uint64_t piece_length) noexcept {
  return offset < piece_length;
}

// ============================================================================
// Protocol Messages
// ============================================================================

export struct KeepAlive {
  [[nodiscard]] friend constexpr bool operator==(KeepAlive const&,
                                                 KeepAlive const&) = default;
};

export struct Choke {
  [[nodiscard]] friend constexpr bool operator==(Choke const&,
                                                 Choke const&) = default;
};

export struct Unchoke {
  [[nodiscard]] friend constexpr bool operator==(Unchoke const&,
                                                 Unchoke const&) = default;
};

export struct Interested {
  [[nodiscard]] friend constexpr bool operator==(Interested const&,
                                                 Interested const&) = default;
};

export struct NotInterested {
  [[nodiscard]] friend constexpr bool operator==(NotInterested const&,
                                                 NotInterested const&) = default;
};

export struct Have {
  std::uint32_t piece_index{};

  [[nodiscard]] friend constexpr bool operator==(Have const&,
                                                 Have const&) = default;
};

export struct Bitfield {
  std::vector<std::byte> bits{};

  Bitfield() = default;

  explicit Bitfield(std::size_t piece_count)
      : bits((piece_count + 7) / 8, std::byte{0}) {}

  [[nodiscard]] bool HasPiece(std::size_t index) const noexcept {
    std::size_t const byte_index{index / 8};
    if (byte_index >= bits.size()) {
      return false;
    }
    std::size_t const bit_index{7 - (index % 8)};  // MSB first
    return (bits[byte_index] & (std::byte{1} << bit_index)) != std::byte{0};
  }

  void SetPiece(std::size_t index) {
    std::size_t const byte_index{index / 8};
    if (byte_index >= bits.size()) {
      bits.resize(byte_index + 1, std::byte{0});
    }
    std::size_t const bit_index{7 - (index % 8)};
    bits[byte_index] |= (std::byte{1} << bit_index);
  }

  void ClearPiece(std::size_t index) noexcept {
    std::size_t const byte_index{index / 8};
    if (byte_index >= bits.size()) {
      return;
    }
    std::size_t const bit_index{7 - (index % 8)};
    bits[byte_index] &= ~(std::byte{1} << bit_index);
  }

  [[nodiscard]] std::size_t CountPieces() const noexcept {
    std::size_t count{0};
    for (std::byte const b : bits) {
      count += static_cast<std::size_t>(std::popcount(std::to_integer<unsigned char>(b)));
    }
    return count;
  }

  [[nodiscard]] std::size_t BitCount() const noexcept { return bits.size() * 8; }

  [[nodiscard]] bool Empty() const noexcept { return bits.empty(); }

  [[nodiscard]] friend bool operator==(Bitfield const&,
                                       Bitfield const&) = default;
};

export struct Request {
  BlockInfo block{};

  [[nodiscard]] friend constexpr bool operator==(Request const&,
                                                 Request const&) = default;
};

export struct Piece {
  std::uint32_t piece_index{};
  std::uint32_t offset{};
  std::vector<std::byte> data{};

  [[nodiscard]] BlockInfo ToBlockInfo() const noexcept {
    return {piece_index, offset, static_cast<std::uint32_t>(data.size())};
  }

  [[nodiscard]] friend bool operator==(Piece const&, Piece const&) = default;
};

export struct Cancel {
  BlockInfo block{};

  [[nodiscard]] friend constexpr bool operator==(Cancel const&,
                                                 Cancel const&) = default;
};

export struct Port {
  std::uint16_t listen_port{};

  [[nodiscard]] friend constexpr bool operator==(Port const&,
                                                 Port const&) = default;
};

// Message variant
export using Message = std::variant<KeepAlive, Choke, Unchoke, Interested,
                                    NotInterested, Have, Bitfield, Request,
                                    Piece, Cancel, Port>;

export [[nodiscard]] inline std::string_view GetMessageName(
    Message const& msg) noexcept {
  return std::visit(
      []<typename T>(T const&) -> std::string_view {
        if constexpr (std::same_as<T, KeepAlive>) return "KeepAlive";
        else if constexpr (std::same_as<T, Choke>) return "Choke";
        else if constexpr (std::same_as<T, Unchoke>) return "Unchoke";
        else if constexpr (std::same_as<T, Interested>) return "Interested";
        else if constexpr (std::same_as<T, NotInterested>) return "NotInterested";
        else if constexpr (std::same_as<T, Have>) return "Have";
        else if constexpr (std::same_as<T, Bitfield>) return "Bitfield";
        else if constexpr (std::same_as<T, Request>) return "Request";
        else if constexpr (std::same_as<T, Piece>) return "Piece";
        else if constexpr (std::same_as<T, Cancel>) return "Cancel";
        else if constexpr (std::same_as<T, Port>) return "Port";
        else return "Unknown";
      },
      msg);
}

// ============================================================================
// Connection State
// ============================================================================

export enum class ConnectionState {
  Disconnected,
  Connecting,
  Handshaking,
  Connected,
  Closing,
  Error
};

export [[nodiscard]] constexpr std::string_view ToString(
    ConnectionState state) noexcept {
  switch (state) {
    case ConnectionState::Disconnected:
      return "Disconnected";
    case ConnectionState::Connecting:
      return "Connecting";
    case ConnectionState::Handshaking:
      return "Handshaking";
    case ConnectionState::Connected:
      return "Connected";
    case ConnectionState::Closing:
      return "Closing";
    case ConnectionState::Error:
      return "Error";
    default:
      return "Unknown";
  }
}

// ============================================================================
// Peer State (Choking/Interest from both perspectives)
// ============================================================================

export struct PeerState {
  bool am_choking{true};       // We are choking the peer
  bool am_interested{false};   // We are interested in peer's pieces
  bool peer_choking{true};     // Peer is choking us
  bool peer_interested{false}; // Peer is interested in our pieces

  // Can we request pieces from peer?
  [[nodiscard]] constexpr bool CanDownload() const noexcept {
    return !peer_choking && am_interested;
  }

  // Can peer request pieces from us?
  [[nodiscard]] constexpr bool CanUpload() const noexcept {
    return !am_choking && peer_interested;
  }

  [[nodiscard]] friend constexpr bool operator==(PeerState const&,
                                                 PeerState const&) = default;
};

// ============================================================================
// Peer Endpoint
// ============================================================================

export struct PeerEndpoint {
  std::string ip{};
  std::uint16_t port{};

  [[nodiscard]] std::string ToString() const {
    return std::format("{}:{}", ip, port);
  }

  [[nodiscard]] bool IsValid() const noexcept {
    return !ip.empty() && port != 0;
  }

  [[nodiscard]] friend bool operator==(PeerEndpoint const&,
                                       PeerEndpoint const&) = default;

  [[nodiscard]] friend auto operator<=>(PeerEndpoint const& lhs,
                                        PeerEndpoint const& rhs) {
    if (auto cmp = lhs.ip <=> rhs.ip; cmp != 0) return cmp;
    return lhs.port <=> rhs.port;
  }
};

// Parse "ip:port" string
export [[nodiscard]] inline std::optional<PeerEndpoint> ParseEndpoint(
    std::string_view str) {
  auto const colon_pos = str.rfind(':');
  if (colon_pos == std::string_view::npos || colon_pos == 0 ||
      colon_pos == str.size() - 1) {
    return std::nullopt;
  }

  std::string const ip{str.substr(0, colon_pos)};
  auto const port_str = str.substr(colon_pos + 1);

  std::uint16_t port{};
  auto [ptr, ec] =
      std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);

  if (ec != std::errc{} || ptr != port_str.data() + port_str.size()) {
    return std::nullopt;
  }

  return PeerEndpoint{ip, port};
}

// ============================================================================
// Peer Statistics
// ============================================================================

export struct PeerStats {
  std::uint64_t bytes_downloaded{};
  std::uint64_t bytes_uploaded{};
  std::uint64_t messages_received{};
  std::uint64_t messages_sent{};
  std::chrono::steady_clock::time_point connected_at{};
  std::chrono::steady_clock::time_point last_received_at{};
  std::chrono::steady_clock::time_point last_sent_at{};

  [[nodiscard]] std::chrono::seconds ConnectionDuration() const noexcept {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - connected_at);
  }

  [[nodiscard]] std::chrono::seconds TimeSinceLastReceived() const noexcept {
    if (last_received_at == std::chrono::steady_clock::time_point{}) {
      return std::chrono::seconds::max();
    }
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - last_received_at);
  }

  // Download rate in bytes/second (simple calculation)
  [[nodiscard]] double DownloadRate() const noexcept {
    auto const duration = ConnectionDuration().count();
    if (duration == 0) return 0.0;
    return static_cast<double>(bytes_downloaded) / static_cast<double>(duration);
  }

  [[nodiscard]] double UploadRate() const noexcept {
    auto const duration = ConnectionDuration().count();
    if (duration == 0) return 0.0;
    return static_cast<double>(bytes_uploaded) / static_cast<double>(duration);
  }
};

// ============================================================================
// Handshake Data
// ============================================================================

export struct HandshakeData {
  ReservedBytes reserved{};
  std::array<std::byte, kInfoHashSize> info_hash{};
  PeerId peer_id{};

  [[nodiscard]] friend bool operator==(HandshakeData const&,
                                       HandshakeData const&) = default;
};

}  // namespace byte_torrent::peer_wire
