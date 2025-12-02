#include <gtest/gtest.h>
import peer_wire;
#include <algorithm>
#include <set>
#include <string>

using namespace byte_torrent::peer_wire;

// ============================================================================
// Protocol Constants Tests
// ============================================================================

TEST(PeerWireConstants, HandshakeSizeIsCorrect) {
  EXPECT_EQ(kHandshakeSize, 68u);
  EXPECT_EQ(kHandshakeSize,
            1 + kProtocolNameLength + kReservedBytesSize + kInfoHashSize + kPeerIdSize);
}

TEST(PeerWireConstants, ProtocolNameMatchesLength) {
  EXPECT_EQ(kProtocolName.size(), kProtocolNameLength);
}

TEST(PeerWireConstants, BlockSizeIsReasonable) {
  EXPECT_EQ(kBlockSize, 16384u);
  EXPECT_LE(kBlockSize, kMaxBlockSize);
}

// ============================================================================
// Reserved Bytes / Extensions Tests
// ============================================================================

TEST(ReservedBytes, DefaultIsZero) {
  ReservedBytes const reserved{};
  for (std::byte const b : reserved) {
    EXPECT_EQ(b, std::byte{0});
  }
}

TEST(ReservedBytes, ExtensionProtocolBitSet) {
  auto const reserved = extensions::WithExtensionProtocol();
  EXPECT_TRUE(extensions::SupportsExtensionProtocol(reserved));
}

TEST(ReservedBytes, ExtensionProtocolBitNotSetByDefault) {
  ReservedBytes const reserved{};
  EXPECT_FALSE(extensions::SupportsExtensionProtocol(reserved));
}

// ============================================================================
// Peer ID Tests
// ============================================================================

TEST(PeerId, GenerateWithDefaultPrefix) {
  auto const id = GeneratePeerId();

  // Check prefix "-BT0001-"
  std::string_view const expected_prefix{"-BT0001-"};
  for (std::size_t i{0}; i < expected_prefix.size(); ++i) {
    EXPECT_EQ(id[i], static_cast<std::byte>(expected_prefix[i]));
  }
}

TEST(PeerId, GenerateWithCustomPrefix) {
  auto const id = GeneratePeerId("-MY1234-");

  std::string_view const expected_prefix{"-MY1234-"};
  for (std::size_t i{0}; i < expected_prefix.size(); ++i) {
    EXPECT_EQ(id[i], static_cast<std::byte>(expected_prefix[i]));
  }
}

TEST(PeerId, GenerateUniqueIds) {
  std::set<PeerId> ids;
  for (int i = 0; i < 100; ++i) {
    ids.insert(GeneratePeerId());
  }
  // Should be highly unlikely to have collisions
  EXPECT_EQ(ids.size(), 100u);
}

TEST(PeerId, ThrowsOnOversizedPrefix) {
  std::string const long_prefix(25, 'X');
  EXPECT_THROW(GeneratePeerId(long_prefix), std::invalid_argument);
}

TEST(PeerId, ToStringPrintable) {
  PeerId id{};
  std::string_view const text{"-BT0001-ABCDEFGHIJKL"};
  for (std::size_t i{0}; i < text.size(); ++i) {
    id[i] = static_cast<std::byte>(text[i]);
  }

  auto const result = PeerIdToString(id);
  EXPECT_EQ(result, text);
}

TEST(PeerId, ToStringNonPrintable) {
  PeerId id{};
  id[0] = std::byte{0x00};
  id[1] = std::byte{0xFF};
  id[2] = std::byte{'A'};

  auto const result = PeerIdToString(id);
  EXPECT_TRUE(result.starts_with("\\x00\\xffA"));
}

// ============================================================================
// MessageId Tests
// ============================================================================

TEST(MessageId, ToStringValues) {
  EXPECT_EQ(ToString(MessageId::Choke), "Choke");
  EXPECT_EQ(ToString(MessageId::Unchoke), "Unchoke");
  EXPECT_EQ(ToString(MessageId::Interested), "Interested");
  EXPECT_EQ(ToString(MessageId::NotInterested), "NotInterested");
  EXPECT_EQ(ToString(MessageId::Have), "Have");
  EXPECT_EQ(ToString(MessageId::Bitfield), "Bitfield");
  EXPECT_EQ(ToString(MessageId::Request), "Request");
  EXPECT_EQ(ToString(MessageId::Piece), "Piece");
  EXPECT_EQ(ToString(MessageId::Cancel), "Cancel");
  EXPECT_EQ(ToString(MessageId::Port), "Port");
  EXPECT_EQ(ToString(MessageId::Extended), "Extended");
}

TEST(MessageId, UnderlyingValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(MessageId::Choke), 0);
  EXPECT_EQ(static_cast<std::uint8_t>(MessageId::Unchoke), 1);
  EXPECT_EQ(static_cast<std::uint8_t>(MessageId::Interested), 2);
  EXPECT_EQ(static_cast<std::uint8_t>(MessageId::NotInterested), 3);
  EXPECT_EQ(static_cast<std::uint8_t>(MessageId::Have), 4);
  EXPECT_EQ(static_cast<std::uint8_t>(MessageId::Bitfield), 5);
  EXPECT_EQ(static_cast<std::uint8_t>(MessageId::Request), 6);
  EXPECT_EQ(static_cast<std::uint8_t>(MessageId::Piece), 7);
  EXPECT_EQ(static_cast<std::uint8_t>(MessageId::Cancel), 8);
  EXPECT_EQ(static_cast<std::uint8_t>(MessageId::Port), 9);
  EXPECT_EQ(static_cast<std::uint8_t>(MessageId::Extended), 20);
}

// ============================================================================
// BlockInfo Tests
// ============================================================================

TEST(BlockInfo, DefaultConstruction) {
  BlockInfo const block{};
  EXPECT_EQ(block.piece_index, 0u);
  EXPECT_EQ(block.offset, 0u);
  EXPECT_EQ(block.length, kBlockSize);
}

TEST(BlockInfo, DesignatedInitialization) {
  BlockInfo const block{.piece_index = 5, .offset = 16384, .length = 8192};
  EXPECT_EQ(block.piece_index, 5u);
  EXPECT_EQ(block.offset, 16384u);
  EXPECT_EQ(block.length, 8192u);
}

TEST(BlockInfo, Equality) {
  BlockInfo const a{.piece_index = 1, .offset = 0, .length = 16384};
  BlockInfo const b{.piece_index = 1, .offset = 0, .length = 16384};
  BlockInfo const c{.piece_index = 2, .offset = 0, .length = 16384};

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

TEST(BlockInfo, Ordering) {
  BlockInfo const a{.piece_index = 0, .offset = 0, .length = 16384};
  BlockInfo const b{.piece_index = 0, .offset = 16384, .length = 16384};
  BlockInfo const c{.piece_index = 1, .offset = 0, .length = 16384};

  EXPECT_LT(a, b);
  EXPECT_LT(b, c);
  EXPECT_LT(a, c);
}

TEST(BlockInfo, ValidationHelpers) {
  EXPECT_TRUE(IsValidBlockLength(kBlockSize));
  EXPECT_TRUE(IsValidBlockLength(1));
  EXPECT_TRUE(IsValidBlockLength(kMaxBlockSize));
  EXPECT_FALSE(IsValidBlockLength(0));
  EXPECT_FALSE(IsValidBlockLength(kMaxBlockSize + 1));

  EXPECT_TRUE(IsValidBlockOffset(0, 16384));
  EXPECT_TRUE(IsValidBlockOffset(16383, 16384));
  EXPECT_FALSE(IsValidBlockOffset(16384, 16384));
}

// ============================================================================
// Bitfield Tests
// ============================================================================

TEST(Bitfield, DefaultConstruction) {
  Bitfield const bf{};
  EXPECT_TRUE(bf.Empty());
  EXPECT_EQ(bf.CountPieces(), 0u);
}

TEST(Bitfield, ConstructionWithPieceCount) {
  Bitfield const bf{100};
  EXPECT_FALSE(bf.Empty());
  EXPECT_EQ(bf.bits.size(), 13u);  // ceil(100/8)
  EXPECT_EQ(bf.CountPieces(), 0u);
}

TEST(Bitfield, SetAndHasPiece) {
  Bitfield bf{64};

  EXPECT_FALSE(bf.HasPiece(0));
  EXPECT_FALSE(bf.HasPiece(7));
  EXPECT_FALSE(bf.HasPiece(63));

  bf.SetPiece(0);
  bf.SetPiece(7);
  bf.SetPiece(63);

  EXPECT_TRUE(bf.HasPiece(0));
  EXPECT_TRUE(bf.HasPiece(7));
  EXPECT_TRUE(bf.HasPiece(63));
  EXPECT_FALSE(bf.HasPiece(1));
  EXPECT_FALSE(bf.HasPiece(62));
}

TEST(Bitfield, ClearPiece) {
  Bitfield bf{16};
  bf.SetPiece(5);
  EXPECT_TRUE(bf.HasPiece(5));

  bf.ClearPiece(5);
  EXPECT_FALSE(bf.HasPiece(5));
}

TEST(Bitfield, CountPieces) {
  Bitfield bf{100};
  EXPECT_EQ(bf.CountPieces(), 0u);

  bf.SetPiece(0);
  EXPECT_EQ(bf.CountPieces(), 1u);

  bf.SetPiece(50);
  bf.SetPiece(99);
  EXPECT_EQ(bf.CountPieces(), 3u);
}

TEST(Bitfield, BitOrderMSBFirst) {
  // BitTorrent spec: bit 0 is MSB of byte 0
  Bitfield bf{16};
  bf.SetPiece(0);

  // Piece 0 should set bit 7 (MSB) of byte 0
  EXPECT_EQ(bf.bits[0], std::byte{0x80});

  bf.SetPiece(7);
  // Piece 7 should set bit 0 (LSB) of byte 0
  EXPECT_EQ(bf.bits[0], std::byte{0x81});
}

TEST(Bitfield, HasPieceOutOfRange) {
  Bitfield const bf{8};
  EXPECT_FALSE(bf.HasPiece(100));
}

TEST(Bitfield, ClearPieceOutOfRange) {
  Bitfield bf{8};
  bf.ClearPiece(100);  // Should not throw
  EXPECT_EQ(bf.bits.size(), 1u);
}

TEST(Bitfield, SetPieceExpandsVector) {
  Bitfield bf{};
  bf.SetPiece(15);
  EXPECT_TRUE(bf.HasPiece(15));
  EXPECT_EQ(bf.bits.size(), 2u);
}

// ============================================================================
// Message Types Tests
// ============================================================================

TEST(Messages, KeepAliveEquality) {
  EXPECT_EQ(KeepAlive{}, KeepAlive{});
}

TEST(Messages, HaveEquality) {
  Have const a{.piece_index = 42};
  Have const b{.piece_index = 42};
  Have const c{.piece_index = 99};

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

TEST(Messages, RequestEquality) {
  Request const a{.block = {.piece_index = 1, .offset = 0, .length = 16384}};
  Request const b{.block = {.piece_index = 1, .offset = 0, .length = 16384}};
  Request const c{.block = {.piece_index = 2, .offset = 0, .length = 16384}};

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

TEST(Messages, PieceToBlockInfo) {
  Piece const piece{.piece_index = 5,
                    .offset = 16384,
                    .data = std::vector<std::byte>(8192)};

  auto const block_info = piece.ToBlockInfo();
  EXPECT_EQ(block_info.piece_index, 5u);
  EXPECT_EQ(block_info.offset, 16384u);
  EXPECT_EQ(block_info.length, 8192u);
}

TEST(Messages, VariantGetMessageName) {
  EXPECT_EQ(GetMessageName(Message{KeepAlive{}}), "KeepAlive");
  EXPECT_EQ(GetMessageName(Message{Choke{}}), "Choke");
  EXPECT_EQ(GetMessageName(Message{Unchoke{}}), "Unchoke");
  EXPECT_EQ(GetMessageName(Message{Interested{}}), "Interested");
  EXPECT_EQ(GetMessageName(Message{NotInterested{}}), "NotInterested");
  EXPECT_EQ(GetMessageName(Message{Have{}}), "Have");
  EXPECT_EQ(GetMessageName(Message{Bitfield{}}), "Bitfield");
  EXPECT_EQ(GetMessageName(Message{Request{}}), "Request");
  EXPECT_EQ(GetMessageName(Message{Piece{}}), "Piece");
  EXPECT_EQ(GetMessageName(Message{Cancel{}}), "Cancel");
  EXPECT_EQ(GetMessageName(Message{Port{}}), "Port");
}

// ============================================================================
// ConnectionState Tests
// ============================================================================

TEST(ConnectionState, ToStringValues) {
  EXPECT_EQ(ToString(ConnectionState::Disconnected), "Disconnected");
  EXPECT_EQ(ToString(ConnectionState::Connecting), "Connecting");
  EXPECT_EQ(ToString(ConnectionState::Handshaking), "Handshaking");
  EXPECT_EQ(ToString(ConnectionState::Connected), "Connected");
  EXPECT_EQ(ToString(ConnectionState::Closing), "Closing");
  EXPECT_EQ(ToString(ConnectionState::Error), "Error");
}

// ============================================================================
// PeerState Tests
// ============================================================================

TEST(PeerState, DefaultState) {
  PeerState const state{};

  EXPECT_TRUE(state.am_choking);
  EXPECT_FALSE(state.am_interested);
  EXPECT_TRUE(state.peer_choking);
  EXPECT_FALSE(state.peer_interested);
}

TEST(PeerState, CanDownload) {
  PeerState state{};

  // Default: peer is choking us
  EXPECT_FALSE(state.CanDownload());

  // Peer unchokes us, but we're not interested
  state.peer_choking = false;
  EXPECT_FALSE(state.CanDownload());

  // We become interested
  state.am_interested = true;
  EXPECT_TRUE(state.CanDownload());

  // Peer chokes us again
  state.peer_choking = true;
  EXPECT_FALSE(state.CanDownload());
}

TEST(PeerState, CanUpload) {
  PeerState state{};

  // Default: we're choking
  EXPECT_FALSE(state.CanUpload());

  // We unchoke, but peer not interested
  state.am_choking = false;
  EXPECT_FALSE(state.CanUpload());

  // Peer becomes interested
  state.peer_interested = true;
  EXPECT_TRUE(state.CanUpload());
}

TEST(PeerState, Equality) {
  PeerState const a{};
  PeerState const b{};
  PeerState c{};
  c.am_interested = true;

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

// ============================================================================
// PeerEndpoint Tests
// ============================================================================

TEST(PeerEndpoint, DefaultConstruction) {
  PeerEndpoint const ep{};
  EXPECT_TRUE(ep.ip.empty());
  EXPECT_EQ(ep.port, 0);
  EXPECT_FALSE(ep.IsValid());
}

TEST(PeerEndpoint, DesignatedInitialization) {
  PeerEndpoint const ep{.ip = "192.168.1.1", .port = 6881};
  EXPECT_EQ(ep.ip, "192.168.1.1");
  EXPECT_EQ(ep.port, 6881);
  EXPECT_TRUE(ep.IsValid());
}

TEST(PeerEndpoint, ToString) {
  PeerEndpoint const ep{.ip = "10.0.0.1", .port = 51413};
  EXPECT_EQ(ep.ToString(), "10.0.0.1:51413");
}

TEST(PeerEndpoint, Equality) {
  PeerEndpoint const a{.ip = "127.0.0.1", .port = 6881};
  PeerEndpoint const b{.ip = "127.0.0.1", .port = 6881};
  PeerEndpoint const c{.ip = "127.0.0.1", .port = 6882};

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

TEST(PeerEndpoint, Ordering) {
  PeerEndpoint const a{.ip = "1.2.3.4", .port = 100};
  PeerEndpoint const b{.ip = "1.2.3.4", .port = 200};
  PeerEndpoint const c{.ip = "1.2.3.5", .port = 100};

  EXPECT_LT(a, b);
  EXPECT_LT(b, c);
}

TEST(PeerEndpoint, ParseValid) {
  auto const ep = ParseEndpoint("192.168.1.100:6881");
  ASSERT_TRUE(ep.has_value());
  EXPECT_EQ(ep->ip, "192.168.1.100");
  EXPECT_EQ(ep->port, 6881);
}

TEST(PeerEndpoint, ParseIPv6LikeString) {
  // Note: This is a simplified parser, not full IPv6 support
  auto const ep = ParseEndpoint("[::1]:6881");
  ASSERT_TRUE(ep.has_value());
  EXPECT_EQ(ep->ip, "[::1]");
  EXPECT_EQ(ep->port, 6881);
}

TEST(PeerEndpoint, ParseInvalidNoPort) {
  EXPECT_FALSE(ParseEndpoint("192.168.1.1").has_value());
  EXPECT_FALSE(ParseEndpoint("192.168.1.1:").has_value());
}

TEST(PeerEndpoint, ParseInvalidNoIp) {
  EXPECT_FALSE(ParseEndpoint(":6881").has_value());
}

TEST(PeerEndpoint, ParseInvalidPort) {
  EXPECT_FALSE(ParseEndpoint("192.168.1.1:abc").has_value());
  EXPECT_FALSE(ParseEndpoint("192.168.1.1:99999").has_value());
}

// ============================================================================
// PeerStats Tests
// ============================================================================

TEST(PeerStats, DefaultConstruction) {
  PeerStats const stats{};
  EXPECT_EQ(stats.bytes_downloaded, 0u);
  EXPECT_EQ(stats.bytes_uploaded, 0u);
  EXPECT_EQ(stats.messages_received, 0u);
  EXPECT_EQ(stats.messages_sent, 0u);
}

TEST(PeerStats, DownloadRateZeroDuration) {
  PeerStats stats{};
  stats.connected_at = std::chrono::steady_clock::now();
  stats.bytes_downloaded = 1000;
  // Zero or near-zero duration should not crash
  EXPECT_GE(stats.DownloadRate(), 0.0);
}

TEST(PeerStats, TimeSinceLastReceivedNeverReceived) {
  PeerStats const stats{};
  EXPECT_EQ(stats.TimeSinceLastReceived(), std::chrono::seconds::max());
}

// ============================================================================
// HandshakeData Tests
// ============================================================================

TEST(HandshakeData, DefaultConstruction) {
  HandshakeData const data{};
  for (std::byte const b : data.reserved) {
    EXPECT_EQ(b, std::byte{0});
  }
  for (std::byte const b : data.info_hash) {
    EXPECT_EQ(b, std::byte{0});
  }
  for (std::byte const b : data.peer_id) {
    EXPECT_EQ(b, std::byte{0});
  }
}

TEST(HandshakeData, Equality) {
  HandshakeData a{};
  HandshakeData b{};
  EXPECT_EQ(a, b);

  a.info_hash[0] = std::byte{0xFF};
  EXPECT_NE(a, b);
}

// ============================================================================
// Message Encoding Tests
// ============================================================================

class MessageEncodingTest : public ::testing::Test {
 protected:
  // Helper to check big-endian uint32
  static std::uint32_t ReadUint32BE(std::span<std::byte const> data) {
    return (std::to_integer<std::uint32_t>(data[0]) << 24) |
           (std::to_integer<std::uint32_t>(data[1]) << 16) |
           (std::to_integer<std::uint32_t>(data[2]) << 8) |
           std::to_integer<std::uint32_t>(data[3]);
  }

  static std::uint16_t ReadUint16BE(std::span<std::byte const> data) {
    return static_cast<std::uint16_t>(
        (std::to_integer<std::uint16_t>(data[0]) << 8) |
        std::to_integer<std::uint16_t>(data[1]));
  }
};

TEST_F(MessageEncodingTest, EncodeKeepAlive) {
  auto const encoded = MessageCodec::Encode(KeepAlive{});

  ASSERT_EQ(encoded.size(), 4u);
  EXPECT_EQ(ReadUint32BE(encoded), 0u);  // length = 0
}

TEST_F(MessageEncodingTest, EncodeChoke) {
  auto const encoded = MessageCodec::Encode(Choke{});

  ASSERT_EQ(encoded.size(), 5u);
  EXPECT_EQ(ReadUint32BE(encoded), 1u);  // length = 1
  EXPECT_EQ(encoded[4], static_cast<std::byte>(MessageId::Choke));
}

TEST_F(MessageEncodingTest, EncodeUnchoke) {
  auto const encoded = MessageCodec::Encode(Unchoke{});

  ASSERT_EQ(encoded.size(), 5u);
  EXPECT_EQ(ReadUint32BE(encoded), 1u);
  EXPECT_EQ(encoded[4], static_cast<std::byte>(MessageId::Unchoke));
}

TEST_F(MessageEncodingTest, EncodeInterested) {
  auto const encoded = MessageCodec::Encode(Interested{});

  ASSERT_EQ(encoded.size(), 5u);
  EXPECT_EQ(ReadUint32BE(encoded), 1u);
  EXPECT_EQ(encoded[4], static_cast<std::byte>(MessageId::Interested));
}

TEST_F(MessageEncodingTest, EncodeNotInterested) {
  auto const encoded = MessageCodec::Encode(NotInterested{});

  ASSERT_EQ(encoded.size(), 5u);
  EXPECT_EQ(ReadUint32BE(encoded), 1u);
  EXPECT_EQ(encoded[4], static_cast<std::byte>(MessageId::NotInterested));
}

TEST_F(MessageEncodingTest, EncodeHave) {
  Have const msg{.piece_index = 0x12345678};
  auto const encoded = MessageCodec::Encode(msg);

  ASSERT_EQ(encoded.size(), 9u);
  EXPECT_EQ(ReadUint32BE(encoded), 5u);  // length = 1 + 4
  EXPECT_EQ(encoded[4], static_cast<std::byte>(MessageId::Have));
  EXPECT_EQ(ReadUint32BE(std::span{encoded}.subspan(5)), 0x12345678u);
}

TEST_F(MessageEncodingTest, EncodeBitfield) {
  Bitfield bf{16};
  bf.SetPiece(0);
  bf.SetPiece(15);
  auto const encoded = MessageCodec::Encode(bf);

  ASSERT_EQ(encoded.size(), 4u + 1u + 2u);  // length prefix + id + 2 bytes
  EXPECT_EQ(ReadUint32BE(encoded), 3u);     // length = 1 + 2
  EXPECT_EQ(encoded[4], static_cast<std::byte>(MessageId::Bitfield));
  EXPECT_EQ(encoded[5], std::byte{0x80});  // Piece 0 = MSB
  EXPECT_EQ(encoded[6], std::byte{0x01});  // Piece 15 = LSB of byte 1
}

TEST_F(MessageEncodingTest, EncodeRequest) {
  Request const msg{.block = {.piece_index = 10, .offset = 16384, .length = 8192}};
  auto const encoded = MessageCodec::Encode(msg);

  ASSERT_EQ(encoded.size(), 17u);
  EXPECT_EQ(ReadUint32BE(encoded), 13u);  // length = 1 + 4 + 4 + 4
  EXPECT_EQ(encoded[4], static_cast<std::byte>(MessageId::Request));
  EXPECT_EQ(ReadUint32BE(std::span{encoded}.subspan(5)), 10u);
  EXPECT_EQ(ReadUint32BE(std::span{encoded}.subspan(9)), 16384u);
  EXPECT_EQ(ReadUint32BE(std::span{encoded}.subspan(13)), 8192u);
}

TEST_F(MessageEncodingTest, EncodePiece) {
  Piece msg{.piece_index = 5, .offset = 0};
  msg.data = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
  auto const encoded = MessageCodec::Encode(msg);

  ASSERT_EQ(encoded.size(), 4u + 1u + 4u + 4u + 4u);  // 17 bytes
  EXPECT_EQ(ReadUint32BE(encoded), 13u);             // length = 1 + 4 + 4 + 4
  EXPECT_EQ(encoded[4], static_cast<std::byte>(MessageId::Piece));
  EXPECT_EQ(ReadUint32BE(std::span{encoded}.subspan(5)), 5u);
  EXPECT_EQ(ReadUint32BE(std::span{encoded}.subspan(9)), 0u);
  EXPECT_EQ(encoded[13], std::byte{0xDE});
  EXPECT_EQ(encoded[14], std::byte{0xAD});
  EXPECT_EQ(encoded[15], std::byte{0xBE});
  EXPECT_EQ(encoded[16], std::byte{0xEF});
}

TEST_F(MessageEncodingTest, EncodeCancel) {
  Cancel const msg{.block = {.piece_index = 1, .offset = 0, .length = 16384}};
  auto const encoded = MessageCodec::Encode(msg);

  ASSERT_EQ(encoded.size(), 17u);
  EXPECT_EQ(ReadUint32BE(encoded), 13u);
  EXPECT_EQ(encoded[4], static_cast<std::byte>(MessageId::Cancel));
  EXPECT_EQ(ReadUint32BE(std::span{encoded}.subspan(5)), 1u);
  EXPECT_EQ(ReadUint32BE(std::span{encoded}.subspan(9)), 0u);
  EXPECT_EQ(ReadUint32BE(std::span{encoded}.subspan(13)), 16384u);
}

TEST_F(MessageEncodingTest, EncodePort) {
  Port const msg{.listen_port = 6881};
  auto const encoded = MessageCodec::Encode(msg);

  ASSERT_EQ(encoded.size(), 7u);
  EXPECT_EQ(ReadUint32BE(encoded), 3u);  // length = 1 + 2
  EXPECT_EQ(encoded[4], static_cast<std::byte>(MessageId::Port));
  EXPECT_EQ(ReadUint16BE(std::span{encoded}.subspan(5)), 6881u);
}

// ============================================================================
// Message Decoding Tests
// ============================================================================

class MessageDecodingTest : public ::testing::Test {
 protected:
  // Helper to write big-endian uint32
  static void WriteUint32BE(std::vector<std::byte>& buf, std::uint32_t value) {
    buf.push_back(static_cast<std::byte>((value >> 24) & 0xFF));
    buf.push_back(static_cast<std::byte>((value >> 16) & 0xFF));
    buf.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>(value & 0xFF));
  }

  static void WriteUint16BE(std::vector<std::byte>& buf, std::uint16_t value) {
    buf.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>(value & 0xFF));
  }
};

TEST_F(MessageDecodingTest, DecodeKeepAlive) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 0);

  auto const result = MessageCodec::Decode(data);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(std::holds_alternative<KeepAlive>(result->message));
  EXPECT_EQ(result->bytes_consumed, 4u);
}

TEST_F(MessageDecodingTest, DecodeChoke) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 1);
  data.push_back(static_cast<std::byte>(MessageId::Choke));

  auto const result = MessageCodec::Decode(data);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(std::holds_alternative<Choke>(result->message));
  EXPECT_EQ(result->bytes_consumed, 5u);
}

TEST_F(MessageDecodingTest, DecodeUnchoke) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 1);
  data.push_back(static_cast<std::byte>(MessageId::Unchoke));

  auto const result = MessageCodec::Decode(data);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(std::holds_alternative<Unchoke>(result->message));
}

TEST_F(MessageDecodingTest, DecodeInterested) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 1);
  data.push_back(static_cast<std::byte>(MessageId::Interested));

  auto const result = MessageCodec::Decode(data);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(std::holds_alternative<Interested>(result->message));
}

TEST_F(MessageDecodingTest, DecodeNotInterested) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 1);
  data.push_back(static_cast<std::byte>(MessageId::NotInterested));

  auto const result = MessageCodec::Decode(data);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(std::holds_alternative<NotInterested>(result->message));
}

TEST_F(MessageDecodingTest, DecodeHave) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 5);
  data.push_back(static_cast<std::byte>(MessageId::Have));
  WriteUint32BE(data, 42);

  auto const result = MessageCodec::Decode(data);

  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(std::holds_alternative<Have>(result->message));
  EXPECT_EQ(std::get<Have>(result->message).piece_index, 42u);
}

TEST_F(MessageDecodingTest, DecodeBitfield) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 3);  // 1 + 2 bytes
  data.push_back(static_cast<std::byte>(MessageId::Bitfield));
  data.push_back(std::byte{0x80});  // Piece 0
  data.push_back(std::byte{0x01});  // Piece 15

  auto const result = MessageCodec::Decode(data);

  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(std::holds_alternative<Bitfield>(result->message));
  auto const& bf = std::get<Bitfield>(result->message);
  EXPECT_TRUE(bf.HasPiece(0));
  EXPECT_TRUE(bf.HasPiece(15));
  EXPECT_FALSE(bf.HasPiece(1));
}

TEST_F(MessageDecodingTest, DecodeRequest) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 13);
  data.push_back(static_cast<std::byte>(MessageId::Request));
  WriteUint32BE(data, 7);      // piece_index
  WriteUint32BE(data, 16384);  // offset
  WriteUint32BE(data, 8192);   // length

  auto const result = MessageCodec::Decode(data);

  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(std::holds_alternative<Request>(result->message));
  auto const& req = std::get<Request>(result->message);
  EXPECT_EQ(req.block.piece_index, 7u);
  EXPECT_EQ(req.block.offset, 16384u);
  EXPECT_EQ(req.block.length, 8192u);
}

TEST_F(MessageDecodingTest, DecodePiece) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 13);  // 1 + 4 + 4 + 4 bytes of payload
  data.push_back(static_cast<std::byte>(MessageId::Piece));
  WriteUint32BE(data, 3);   // piece_index
  WriteUint32BE(data, 0);   // offset
  data.push_back(std::byte{0xAB});
  data.push_back(std::byte{0xCD});
  data.push_back(std::byte{0xEF});
  data.push_back(std::byte{0x01});

  auto const result = MessageCodec::Decode(data);

  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(std::holds_alternative<Piece>(result->message));
  auto const& piece = std::get<Piece>(result->message);
  EXPECT_EQ(piece.piece_index, 3u);
  EXPECT_EQ(piece.offset, 0u);
  ASSERT_EQ(piece.data.size(), 4u);
  EXPECT_EQ(piece.data[0], std::byte{0xAB});
}

TEST_F(MessageDecodingTest, DecodeCancel) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 13);
  data.push_back(static_cast<std::byte>(MessageId::Cancel));
  WriteUint32BE(data, 1);
  WriteUint32BE(data, 0);
  WriteUint32BE(data, 16384);

  auto const result = MessageCodec::Decode(data);

  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(std::holds_alternative<Cancel>(result->message));
  auto const& cancel = std::get<Cancel>(result->message);
  EXPECT_EQ(cancel.block.piece_index, 1u);
}

TEST_F(MessageDecodingTest, DecodePort) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 3);
  data.push_back(static_cast<std::byte>(MessageId::Port));
  WriteUint16BE(data, 51413);

  auto const result = MessageCodec::Decode(data);

  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(std::holds_alternative<Port>(result->message));
  EXPECT_EQ(std::get<Port>(result->message).listen_port, 51413u);
}

TEST_F(MessageDecodingTest, IncompleteHeader) {
  std::vector<std::byte> data{std::byte{0x00}, std::byte{0x00}};  // Only 2 bytes
  auto const result = MessageCodec::Decode(data);
  EXPECT_FALSE(result.has_value());
}

TEST_F(MessageDecodingTest, IncompletePayload) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 5);  // Expects 5 bytes payload
  data.push_back(static_cast<std::byte>(MessageId::Have));
  // Missing 4 bytes of piece_index

  auto const result = MessageCodec::Decode(data);
  EXPECT_FALSE(result.has_value());
}

TEST_F(MessageDecodingTest, InvalidMessageId) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 1);
  data.push_back(std::byte{0xFF});  // Invalid ID

  EXPECT_THROW(MessageCodec::Decode(data), std::invalid_argument);
}

TEST_F(MessageDecodingTest, InvalidPayloadSizeForHave) {
  std::vector<std::byte> data;
  WriteUint32BE(data, 3);  // Wrong size (should be 5)
  data.push_back(static_cast<std::byte>(MessageId::Have));
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x00});

  EXPECT_THROW(MessageCodec::Decode(data), std::invalid_argument);
}

// ============================================================================
// Handshake Encoding/Decoding Tests
// ============================================================================

class HandshakeTest : public ::testing::Test {
 protected:
  static std::array<std::byte, kInfoHashSize> MakeInfoHash(std::byte fill) {
    std::array<std::byte, kInfoHashSize> hash{};
    std::ranges::fill(hash, fill);
    return hash;
  }

  static PeerId MakePeerId(std::string_view prefix) {
    PeerId id{};
    for (std::size_t i = 0; i < std::min(prefix.size(), kPeerIdSize); ++i) {
      id[i] = static_cast<std::byte>(prefix[i]);
    }
    return id;
  }
};

TEST_F(HandshakeTest, EncodeHandshake) {
  auto const info_hash = MakeInfoHash(std::byte{0xAB});
  auto const peer_id = MakePeerId("-BT0001-");
  auto const reserved = extensions::WithExtensionProtocol();

  auto const encoded =
      MessageCodec::EncodeHandshake(info_hash, peer_id, reserved);

  ASSERT_EQ(encoded.size(), kHandshakeSize);

  // Protocol name length
  EXPECT_EQ(encoded[0], static_cast<std::byte>(kProtocolNameLength));

  // Protocol name
  std::string_view const protocol{reinterpret_cast<char const*>(&encoded[1]),
                                  kProtocolNameLength};
  EXPECT_EQ(protocol, kProtocolName);

  // Reserved bytes
  EXPECT_EQ(encoded[20], std::byte{0x00});
  EXPECT_EQ(encoded[25], std::byte{0x10});  // Extension bit

  // Info hash
  for (std::size_t i = 0; i < kInfoHashSize; ++i) {
    EXPECT_EQ(encoded[28 + i], std::byte{0xAB});
  }

  // Peer ID prefix
  EXPECT_EQ(encoded[48], static_cast<std::byte>('-'));
  EXPECT_EQ(encoded[49], static_cast<std::byte>('B'));
}

TEST_F(HandshakeTest, DecodeHandshake) {
  auto const info_hash = MakeInfoHash(std::byte{0xCD});
  auto const peer_id = MakePeerId("-TR2940-");

  auto const encoded = MessageCodec::EncodeHandshake(info_hash, peer_id);
  auto const decoded = MessageCodec::DecodeHandshake(encoded);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->info_hash, info_hash);
  EXPECT_EQ(decoded->peer_id, peer_id);
}

TEST_F(HandshakeTest, DecodeHandshakeIncomplete) {
  std::vector<std::byte> partial(50);  // Less than 68 bytes
  auto const decoded = MessageCodec::DecodeHandshake(partial);
  EXPECT_FALSE(decoded.has_value());
}

TEST_F(HandshakeTest, DecodeHandshakeInvalidProtocolLength) {
  std::vector<std::byte> data(kHandshakeSize);
  data[0] = std::byte{20};  // Wrong length

  EXPECT_THROW(MessageCodec::DecodeHandshake(data), std::invalid_argument);
}

TEST_F(HandshakeTest, DecodeHandshakeInvalidProtocolName) {
  std::vector<std::byte> data(kHandshakeSize);
  data[0] = static_cast<std::byte>(kProtocolNameLength);
  // Fill with wrong protocol name
  for (std::size_t i = 0; i < kProtocolNameLength; ++i) {
    data[1 + i] = static_cast<std::byte>('X');
  }

  EXPECT_THROW(MessageCodec::DecodeHandshake(data), std::invalid_argument);
}

TEST_F(HandshakeTest, RoundTrip) {
  auto const info_hash = MakeInfoHash(std::byte{0x12});
  auto const peer_id = GeneratePeerId("-BT0001-");
  auto const reserved = extensions::WithExtensionProtocol();

  auto const encoded =
      MessageCodec::EncodeHandshake(info_hash, peer_id, reserved);
  auto const decoded = MessageCodec::DecodeHandshake(encoded);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->info_hash, info_hash);
  EXPECT_EQ(decoded->peer_id, peer_id);
  EXPECT_TRUE(extensions::SupportsExtensionProtocol(decoded->reserved));
}

// ============================================================================
// Round-Trip Tests (Encode then Decode)
// ============================================================================

class MessageRoundTripTest : public ::testing::Test {};

TEST_F(MessageRoundTripTest, KeepAlive) {
  Message const original{KeepAlive{}};
  auto const encoded = MessageCodec::Encode(original);
  auto const decoded = MessageCodec::Decode(encoded);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(std::holds_alternative<KeepAlive>(decoded->message));
}

TEST_F(MessageRoundTripTest, Choke) {
  Message const original{Choke{}};
  auto const encoded = MessageCodec::Encode(original);
  auto const decoded = MessageCodec::Decode(encoded);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(std::holds_alternative<Choke>(decoded->message));
}

TEST_F(MessageRoundTripTest, Have) {
  Message const original{Have{.piece_index = 12345}};
  auto const encoded = MessageCodec::Encode(original);
  auto const decoded = MessageCodec::Decode(encoded);

  ASSERT_TRUE(decoded.has_value());
  ASSERT_TRUE(std::holds_alternative<Have>(decoded->message));
  EXPECT_EQ(std::get<Have>(decoded->message).piece_index, 12345u);
}

TEST_F(MessageRoundTripTest, Bitfield) {
  Bitfield bf{100};
  bf.SetPiece(0);
  bf.SetPiece(50);
  bf.SetPiece(99);

  Message const original{bf};
  auto const encoded = MessageCodec::Encode(original);
  auto const decoded = MessageCodec::Decode(encoded);

  ASSERT_TRUE(decoded.has_value());
  ASSERT_TRUE(std::holds_alternative<Bitfield>(decoded->message));
  auto const& result = std::get<Bitfield>(decoded->message);
  EXPECT_TRUE(result.HasPiece(0));
  EXPECT_TRUE(result.HasPiece(50));
  EXPECT_TRUE(result.HasPiece(99));
  EXPECT_FALSE(result.HasPiece(1));
}

TEST_F(MessageRoundTripTest, Request) {
  Message const original{
      Request{.block = {.piece_index = 7, .offset = 32768, .length = 16384}}};
  auto const encoded = MessageCodec::Encode(original);
  auto const decoded = MessageCodec::Decode(encoded);

  ASSERT_TRUE(decoded.has_value());
  ASSERT_TRUE(std::holds_alternative<Request>(decoded->message));
  auto const& req = std::get<Request>(decoded->message);
  EXPECT_EQ(req.block.piece_index, 7u);
  EXPECT_EQ(req.block.offset, 32768u);
  EXPECT_EQ(req.block.length, 16384u);
}

TEST_F(MessageRoundTripTest, Cancel) {
  Message const original{
      Cancel{.block = {.piece_index = 100, .offset = 0, .length = 8192}}};
  auto const encoded = MessageCodec::Encode(original);
  auto const decoded = MessageCodec::Decode(encoded);

  ASSERT_TRUE(decoded.has_value());
  ASSERT_TRUE(std::holds_alternative<Cancel>(decoded->message));
  EXPECT_EQ(std::get<Cancel>(decoded->message).block,
            (BlockInfo{.piece_index = 100, .offset = 0, .length = 8192}));
}

TEST_F(MessageRoundTripTest, Port) {
  Message const original{Port{.listen_port = 51413}};
  auto const encoded = MessageCodec::Encode(original);
  auto const decoded = MessageCodec::Decode(encoded);

  ASSERT_TRUE(decoded.has_value());
  ASSERT_TRUE(std::holds_alternative<Port>(decoded->message));
  EXPECT_EQ(std::get<Port>(decoded->message).listen_port, 51413u);
}

// ============================================================================
// Multiple Messages in Buffer Test
// ============================================================================

TEST(MessageStream, DecodeMultipleMessages) {
  std::vector<std::byte> buffer;

  // Append KeepAlive
  auto msg1 = MessageCodec::Encode(KeepAlive{});
  buffer.insert(buffer.end(), msg1.begin(), msg1.end());

  // Append Have
  auto msg2 = MessageCodec::Encode(Have{.piece_index = 42});
  buffer.insert(buffer.end(), msg2.begin(), msg2.end());

  // Append Interested
  auto msg3 = MessageCodec::Encode(Interested{});
  buffer.insert(buffer.end(), msg3.begin(), msg3.end());

  std::span<std::byte const> remaining{buffer};

  // Decode first
  auto result1 = MessageCodec::Decode(remaining);
  ASSERT_TRUE(result1.has_value());
  EXPECT_TRUE(std::holds_alternative<KeepAlive>(result1->message));
  remaining = remaining.subspan(result1->bytes_consumed);

  // Decode second
  auto result2 = MessageCodec::Decode(remaining);
  ASSERT_TRUE(result2.has_value());
  ASSERT_TRUE(std::holds_alternative<Have>(result2->message));
  EXPECT_EQ(std::get<Have>(result2->message).piece_index, 42u);
  remaining = remaining.subspan(result2->bytes_consumed);

  // Decode third
  auto result3 = MessageCodec::Decode(remaining);
  ASSERT_TRUE(result3.has_value());
  EXPECT_TRUE(std::holds_alternative<Interested>(result3->message));
  remaining = remaining.subspan(result3->bytes_consumed);

  // Buffer should be empty
  EXPECT_TRUE(remaining.empty());
}

TEST(MessageStream, GetMessageLengthPartialHeader) {
  std::vector<std::byte> partial{std::byte{0x00}, std::byte{0x00}};
  auto const length = MessageCodec::GetMessageLength(partial);
  EXPECT_FALSE(length.has_value());
}

TEST(MessageStream, GetMessageLengthValid) {
  std::vector<std::byte> data{std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                              std::byte{0x0D}};  // 13 in big-endian
  auto const length = MessageCodec::GetMessageLength(data);
  ASSERT_TRUE(length.has_value());
  EXPECT_EQ(*length, 13u);
}

// ============================================================================
// Convenience Function Tests
// ============================================================================

TEST(ConvenienceFunctions, EncodeMessage) {
  auto const encoded = EncodeMessage(Choke{});
  EXPECT_EQ(encoded.size(), 5u);
}

TEST(ConvenienceFunctions, DecodeMessage) {
  auto const encoded = EncodeMessage(Unchoke{});
  auto const decoded = DecodeMessage(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(std::holds_alternative<Unchoke>(decoded->message));
}
