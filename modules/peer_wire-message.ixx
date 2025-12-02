export module peer_wire:message;

import std;
import :types;

namespace byte_torrent::peer_wire {

// ============================================================================
// Byte Order Utilities (Network byte order = Big-endian)
// ============================================================================

namespace detail {

template <std::integral T>
[[nodiscard]] constexpr T ByteSwap(T value) noexcept {
  if constexpr (sizeof(T) == 1) {
    return value;
  } else if constexpr (sizeof(T) == 2) {
    return static_cast<T>((value >> 8) | (value << 8));
  } else if constexpr (sizeof(T) == 4) {
    return static_cast<T>(((value >> 24) & 0x000000FF) |
                          ((value >> 8) & 0x0000FF00) |
                          ((value << 8) & 0x00FF0000) |
                          ((value << 24) & 0xFF000000));
  } else {
    static_assert(sizeof(T) <= 4, "Only up to 32-bit integers supported");
  }
}

template <std::integral T>
[[nodiscard]] constexpr T HostToNetwork(T value) noexcept {
  if constexpr (std::endian::native == std::endian::little) {
    return ByteSwap(value);
  } else {
    return value;
  }
}

template <std::integral T>
[[nodiscard]] constexpr T NetworkToHost(T value) noexcept {
  return HostToNetwork(value);  // Same operation
}

// Write integer in big-endian to buffer
template <std::integral T>
void WriteBigEndian(std::vector<std::byte>& buffer, T value) {
  T const network_value{HostToNetwork(value)};
  auto const* bytes = reinterpret_cast<std::byte const*>(&network_value);
  buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
}

// Read integer in big-endian from span
template <std::integral T>
[[nodiscard]] T ReadBigEndian(std::span<std::byte const> data) {
  if (data.size() < sizeof(T)) {
    throw std::invalid_argument{"Insufficient data for integer read"};
  }
  T value{};
  std::memcpy(&value, data.data(), sizeof(T));
  return NetworkToHost(value);
}

}  // namespace detail

// ============================================================================
// Message Codec
// ============================================================================

export class MessageCodec {
 public:
  // Result of decoding attempt
  struct DecodeResult {
    Message message{};
    std::size_t bytes_consumed{};
  };

  // -------------------------------------------------------------------------
  // Encoding
  // -------------------------------------------------------------------------

  [[nodiscard]] static std::vector<std::byte> Encode(Message const& msg) {
    return std::visit(
        [](auto const& m) { return EncodeImpl(m); }, msg);
  }

  [[nodiscard]] static std::vector<std::byte> EncodeHandshake(
      std::span<std::byte const, kInfoHashSize> info_hash,
      PeerId const& peer_id,
      ReservedBytes const& reserved = {}) {
    std::vector<std::byte> buffer;
    buffer.reserve(kHandshakeSize);

    // Protocol name length
    buffer.push_back(static_cast<std::byte>(kProtocolNameLength));

    // Protocol name
    for (char const c : kProtocolName) {
      buffer.push_back(static_cast<std::byte>(c));
    }

    // Reserved bytes
    buffer.insert(buffer.end(), reserved.begin(), reserved.end());

    // Info hash
    buffer.insert(buffer.end(), info_hash.begin(), info_hash.end());

    // Peer ID
    buffer.insert(buffer.end(), peer_id.begin(), peer_id.end());

    return buffer;
  }

  // -------------------------------------------------------------------------
  // Decoding
  // -------------------------------------------------------------------------

  // Returns nullopt if buffer doesn't contain a complete message
  [[nodiscard]] static std::optional<DecodeResult> Decode(
      std::span<std::byte const> buffer) {
    auto const msg_length = GetMessageLength(buffer);
    if (!msg_length) {
      return std::nullopt;  // Incomplete
    }

    std::size_t const total_size{kMessageLengthSize + *msg_length};
    if (buffer.size() < total_size) {
      return std::nullopt;  // Incomplete
    }

    // Keep-alive: length == 0
    if (*msg_length == 0) {
      return DecodeResult{.message = KeepAlive{},
                          .bytes_consumed = kMessageLengthSize};
    }

    // Message ID
    auto const id = static_cast<MessageId>(buffer[kMessageLengthSize]);
    auto const payload =
        buffer.subspan(kMessageLengthSize + kMessageIdSize,
                       *msg_length - kMessageIdSize);

    Message message = DecodePayload(id, payload);
    return DecodeResult{.message = std::move(message),
                        .bytes_consumed = total_size};
  }

  // Get message length from buffer (returns nullopt if incomplete header)
  [[nodiscard]] static std::optional<std::size_t> GetMessageLength(
      std::span<std::byte const> buffer) noexcept {
    if (buffer.size() < kMessageLengthSize) {
      return std::nullopt;
    }
    auto const length = detail::ReadBigEndian<std::uint32_t>(buffer);
    if (length > kMaxMessageSize) {
      return std::nullopt;  // Invalid/too large
    }
    return static_cast<std::size_t>(length);
  }

  // Decode handshake (returns nullopt if incomplete)
  [[nodiscard]] static std::optional<HandshakeData> DecodeHandshake(
      std::span<std::byte const> buffer) {
    if (buffer.size() < kHandshakeSize) {
      return std::nullopt;
    }

    std::size_t offset{0};

    // Protocol name length
    auto const pstrlen = std::to_integer<std::uint8_t>(buffer[offset++]);
    if (pstrlen != kProtocolNameLength) {
      throw std::invalid_argument{
          std::format("Invalid protocol name length: {}", pstrlen)};
    }

    // Protocol name
    std::string_view const protocol_name{
        reinterpret_cast<char const*>(buffer.data() + offset),
        kProtocolNameLength};
    if (protocol_name != kProtocolName) {
      throw std::invalid_argument{"Invalid protocol name"};
    }
    offset += kProtocolNameLength;

    HandshakeData result{};

    // Reserved bytes
    std::copy_n(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                kReservedBytesSize, result.reserved.begin());
    offset += kReservedBytesSize;

    // Info hash
    std::copy_n(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                kInfoHashSize, result.info_hash.begin());
    offset += kInfoHashSize;

    // Peer ID
    std::copy_n(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                kPeerIdSize, result.peer_id.begin());

    return result;
  }

 private:
  // -------------------------------------------------------------------------
  // Encoding Implementations
  // -------------------------------------------------------------------------

  [[nodiscard]] static std::vector<std::byte> EncodeImpl(KeepAlive const&) {
    std::vector<std::byte> buffer;
    buffer.reserve(kMessageLengthSize);
    detail::WriteBigEndian<std::uint32_t>(buffer, 0);
    return buffer;
  }

  [[nodiscard]] static std::vector<std::byte> EncodeImpl(Choke const&) {
    return EncodeSimple(MessageId::Choke);
  }

  [[nodiscard]] static std::vector<std::byte> EncodeImpl(Unchoke const&) {
    return EncodeSimple(MessageId::Unchoke);
  }

  [[nodiscard]] static std::vector<std::byte> EncodeImpl(Interested const&) {
    return EncodeSimple(MessageId::Interested);
  }

  [[nodiscard]] static std::vector<std::byte> EncodeImpl(NotInterested const&) {
    return EncodeSimple(MessageId::NotInterested);
  }

  [[nodiscard]] static std::vector<std::byte> EncodeImpl(Have const& msg) {
    std::vector<std::byte> buffer;
    buffer.reserve(kMessageLengthSize + kMessageIdSize + 4);
    detail::WriteBigEndian<std::uint32_t>(buffer, 5);  // length = 1 + 4
    buffer.push_back(static_cast<std::byte>(MessageId::Have));
    detail::WriteBigEndian<std::uint32_t>(buffer, msg.piece_index);
    return buffer;
  }

  [[nodiscard]] static std::vector<std::byte> EncodeImpl(Bitfield const& msg) {
    std::vector<std::byte> buffer;
    std::uint32_t const length{
        static_cast<std::uint32_t>(1 + msg.bits.size())};
    buffer.reserve(kMessageLengthSize + length);
    detail::WriteBigEndian<std::uint32_t>(buffer, length);
    buffer.push_back(static_cast<std::byte>(MessageId::Bitfield));
    buffer.insert(buffer.end(), msg.bits.begin(), msg.bits.end());
    return buffer;
  }

  [[nodiscard]] static std::vector<std::byte> EncodeImpl(Request const& msg) {
    return EncodeBlockMessage(MessageId::Request, msg.block);
  }

  [[nodiscard]] static std::vector<std::byte> EncodeImpl(Piece const& msg) {
    std::vector<std::byte> buffer;
    std::uint32_t const length{
        static_cast<std::uint32_t>(1 + 4 + 4 + msg.data.size())};
    buffer.reserve(kMessageLengthSize + length);
    detail::WriteBigEndian<std::uint32_t>(buffer, length);
    buffer.push_back(static_cast<std::byte>(MessageId::Piece));
    detail::WriteBigEndian<std::uint32_t>(buffer, msg.piece_index);
    detail::WriteBigEndian<std::uint32_t>(buffer, msg.offset);
    buffer.insert(buffer.end(), msg.data.begin(), msg.data.end());
    return buffer;
  }

  [[nodiscard]] static std::vector<std::byte> EncodeImpl(Cancel const& msg) {
    return EncodeBlockMessage(MessageId::Cancel, msg.block);
  }

  [[nodiscard]] static std::vector<std::byte> EncodeImpl(Port const& msg) {
    std::vector<std::byte> buffer;
    buffer.reserve(kMessageLengthSize + kMessageIdSize + 2);
    detail::WriteBigEndian<std::uint32_t>(buffer, 3);  // length = 1 + 2
    buffer.push_back(static_cast<std::byte>(MessageId::Port));
    detail::WriteBigEndian<std::uint16_t>(buffer, msg.listen_port);
    return buffer;
  }

  // Helper for messages with no payload
  [[nodiscard]] static std::vector<std::byte> EncodeSimple(MessageId id) {
    std::vector<std::byte> buffer;
    buffer.reserve(kMessageLengthSize + kMessageIdSize);
    detail::WriteBigEndian<std::uint32_t>(buffer, 1);  // length = 1
    buffer.push_back(static_cast<std::byte>(id));
    return buffer;
  }

  // Helper for Request/Cancel (same format)
  [[nodiscard]] static std::vector<std::byte> EncodeBlockMessage(
      MessageId id, BlockInfo const& block) {
    std::vector<std::byte> buffer;
    buffer.reserve(kMessageLengthSize + kMessageIdSize + 12);
    detail::WriteBigEndian<std::uint32_t>(buffer, 13);  // length = 1 + 4 + 4 + 4
    buffer.push_back(static_cast<std::byte>(id));
    detail::WriteBigEndian<std::uint32_t>(buffer, block.piece_index);
    detail::WriteBigEndian<std::uint32_t>(buffer, block.offset);
    detail::WriteBigEndian<std::uint32_t>(buffer, block.length);
    return buffer;
  }

  // -------------------------------------------------------------------------
  // Decoding Implementations
  // -------------------------------------------------------------------------

  [[nodiscard]] static Message DecodePayload(MessageId id,
                                             std::span<std::byte const> payload) {
    switch (id) {
      case MessageId::Choke:
        ValidatePayloadSize(payload, 0, "Choke");
        return Choke{};

      case MessageId::Unchoke:
        ValidatePayloadSize(payload, 0, "Unchoke");
        return Unchoke{};

      case MessageId::Interested:
        ValidatePayloadSize(payload, 0, "Interested");
        return Interested{};

      case MessageId::NotInterested:
        ValidatePayloadSize(payload, 0, "NotInterested");
        return NotInterested{};

      case MessageId::Have:
        ValidatePayloadSize(payload, 4, "Have");
        return Have{.piece_index = detail::ReadBigEndian<std::uint32_t>(payload)};

      case MessageId::Bitfield:
        return DecodeBitfield(payload);

      case MessageId::Request:
        ValidatePayloadSize(payload, 12, "Request");
        return Request{.block = DecodeBlockInfo(payload)};

      case MessageId::Piece:
        return DecodePiece(payload);

      case MessageId::Cancel:
        ValidatePayloadSize(payload, 12, "Cancel");
        return Cancel{.block = DecodeBlockInfo(payload)};

      case MessageId::Port:
        ValidatePayloadSize(payload, 2, "Port");
        return Port{.listen_port = detail::ReadBigEndian<std::uint16_t>(payload)};

      default:
        throw std::invalid_argument{
            std::format("Unknown message ID: {}", static_cast<int>(id))};
    }
  }

  static void ValidatePayloadSize(std::span<std::byte const> payload,
                                  std::size_t expected,
                                  std::string_view msg_name) {
    if (payload.size() != expected) {
      throw std::invalid_argument{
          std::format("{} message has invalid payload size: {} (expected {})",
                      msg_name, payload.size(), expected)};
    }
  }

  [[nodiscard]] static BlockInfo DecodeBlockInfo(
      std::span<std::byte const> payload) {
    return BlockInfo{
        .piece_index = detail::ReadBigEndian<std::uint32_t>(payload),
        .offset = detail::ReadBigEndian<std::uint32_t>(payload.subspan(4)),
        .length = detail::ReadBigEndian<std::uint32_t>(payload.subspan(8))};
  }

  [[nodiscard]] static Bitfield DecodeBitfield(
      std::span<std::byte const> payload) {
    Bitfield bf{};
    bf.bits.assign(payload.begin(), payload.end());
    return bf;
  }

  [[nodiscard]] static Piece DecodePiece(std::span<std::byte const> payload) {
    if (payload.size() < 8) {
      throw std::invalid_argument{
          std::format("Piece message too short: {}", payload.size())};
    }
    Piece piece{};
    piece.piece_index = detail::ReadBigEndian<std::uint32_t>(payload);
    piece.offset = detail::ReadBigEndian<std::uint32_t>(payload.subspan(4));
    piece.data.assign(payload.begin() + 8, payload.end());
    return piece;
  }
};

// ============================================================================
// Convenience Functions
// ============================================================================

export [[nodiscard]] inline std::vector<std::byte> EncodeMessage(
    Message const& msg) {
  return MessageCodec::Encode(msg);
}

export [[nodiscard]] inline std::optional<MessageCodec::DecodeResult>
DecodeMessage(std::span<std::byte const> buffer) {
  return MessageCodec::Decode(buffer);
}

export [[nodiscard]] inline std::vector<std::byte> EncodeHandshake(
    std::span<std::byte const, kInfoHashSize> info_hash,
    PeerId const& peer_id,
    ReservedBytes const& reserved = {}) {
  return MessageCodec::EncodeHandshake(info_hash, peer_id, reserved);
}

export [[nodiscard]] inline std::optional<HandshakeData> DecodeHandshake(
    std::span<std::byte const> buffer) {
  return MessageCodec::DecodeHandshake(buffer);
}

}  // namespace byte_torrent::peer_wire
