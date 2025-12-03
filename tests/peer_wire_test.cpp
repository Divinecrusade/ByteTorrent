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

// ============================================================================
// ReceiveBuffer Tests
// ============================================================================

class ReceiveBufferTest : public ::testing::Test {
 protected:
  static std::vector<std::byte> ToBytes(std::string_view str) {
    std::vector<std::byte> result(str.size());
    std::transform(str.begin(), str.end(), result.begin(),
                   [](char c) { return static_cast<std::byte>(c); });
    return result;
  }
};

TEST_F(ReceiveBufferTest, DefaultConstruction) {
  ReceiveBuffer buffer{};
  EXPECT_TRUE(buffer.Empty());
  EXPECT_EQ(buffer.AvailableBytes(), 0u);
}

TEST_F(ReceiveBufferTest, AppendAndRead) {
  ReceiveBuffer buffer{};
  auto data = ToBytes("hello");
  
  buffer.Append(data);
  
  EXPECT_FALSE(buffer.Empty());
  EXPECT_EQ(buffer.AvailableBytes(), 5u);
  
  auto available = buffer.AvailableData();
  EXPECT_EQ(available.size(), 5u);
}

TEST_F(ReceiveBufferTest, GetWriteRegion) {
  ReceiveBuffer buffer{1024};
  
  auto region = buffer.GetWriteRegion(100);
  EXPECT_EQ(region.size(), 100u);
  
  // Simulate socket read
  std::fill(region.begin(), region.end(), std::byte{0xAB});
  buffer.CommitWrite(100);
  
  EXPECT_EQ(buffer.AvailableBytes(), 100u);
}

TEST_F(ReceiveBufferTest, TryExtractMessage) {
  ReceiveBuffer buffer{};
  
  // Append a complete Choke message
  auto msg = MessageCodec::Encode(Choke{});
  buffer.Append(msg);
  
  auto result = buffer.TryExtractMessage();
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(std::holds_alternative<Choke>(result->message));
  EXPECT_TRUE(buffer.Empty());
}

TEST_F(ReceiveBufferTest, TryExtractIncompleteMessage) {
  ReceiveBuffer buffer{};
  
  // Append partial message (just length prefix)
  std::array<std::byte, 2> partial{std::byte{0}, std::byte{0}};
  buffer.Append(partial);
  
  auto result = buffer.TryExtractMessage();
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(buffer.AvailableBytes(), 2u);
}

TEST_F(ReceiveBufferTest, TryExtractHandshake) {
  ReceiveBuffer buffer{};
  
  std::array<std::byte, kInfoHashSize> info_hash{};
  std::fill(info_hash.begin(), info_hash.end(), std::byte{0xAB});
  auto peer_id = GeneratePeerId("-BT0001-");
  
  auto handshake = MessageCodec::EncodeHandshake(info_hash, peer_id);
  buffer.Append(handshake);
  
  auto result = buffer.TryExtractHandshake();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->info_hash, info_hash);
  EXPECT_TRUE(buffer.Empty());
}

TEST_F(ReceiveBufferTest, MultipleMessages) {
  ReceiveBuffer buffer{};
  
  // Append multiple messages
  buffer.Append(MessageCodec::Encode(Choke{}));
  buffer.Append(MessageCodec::Encode(Unchoke{}));
  buffer.Append(MessageCodec::Encode(Interested{}));
  
  auto msg1 = buffer.TryExtractMessage();
  ASSERT_TRUE(msg1.has_value());
  EXPECT_TRUE(std::holds_alternative<Choke>(msg1->message));
  
  auto msg2 = buffer.TryExtractMessage();
  ASSERT_TRUE(msg2.has_value());
  EXPECT_TRUE(std::holds_alternative<Unchoke>(msg2->message));
  
  auto msg3 = buffer.TryExtractMessage();
  ASSERT_TRUE(msg3.has_value());
  EXPECT_TRUE(std::holds_alternative<Interested>(msg3->message));
  
  EXPECT_TRUE(buffer.Empty());
}

TEST_F(ReceiveBufferTest, Clear) {
  ReceiveBuffer buffer{};
  buffer.Append(ToBytes("test data"));
  
  EXPECT_FALSE(buffer.Empty());
  buffer.Clear();
  EXPECT_TRUE(buffer.Empty());
}

TEST_F(ReceiveBufferTest, PeekMessageLength) {
  ReceiveBuffer buffer{};
  
  // No data
  EXPECT_FALSE(buffer.PeekMessageLength().has_value());
  
  // Add message
  buffer.Append(MessageCodec::Encode(Have{.piece_index = 42}));
  
  auto length = buffer.PeekMessageLength();
  ASSERT_TRUE(length.has_value());
  EXPECT_EQ(*length, 5u);  // 1 byte ID + 4 bytes piece_index
}

// ============================================================================
// SendBuffer Tests
// ============================================================================

TEST(SendBufferTest, EnqueueAndDequeue) {
  SendBuffer buffer{};
  
  EXPECT_TRUE(buffer.Empty());
  
  buffer.Enqueue(Choke{});
  EXPECT_FALSE(buffer.Empty());
  EXPECT_EQ(buffer.Size(), 1u);
  
  auto data = buffer.Dequeue();
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(data->size(), 5u);  // 4 byte length + 1 byte ID
  EXPECT_TRUE(buffer.Empty());
}

TEST(SendBufferTest, EnqueueMultiple) {
  SendBuffer buffer{};
  
  buffer.Enqueue(Choke{});
  buffer.Enqueue(Unchoke{});
  buffer.Enqueue(Have{.piece_index = 10});
  
  EXPECT_EQ(buffer.Size(), 3u);
  
  buffer.Dequeue();
  buffer.Dequeue();
  buffer.Dequeue();
  
  EXPECT_TRUE(buffer.Empty());
}

TEST(SendBufferTest, EnqueueHandshake) {
  SendBuffer buffer{};
  
  std::array<std::byte, kInfoHashSize> info_hash{};
  PeerId peer_id{};
  
  buffer.EnqueueHandshake(info_hash, peer_id);
  
  auto data = buffer.Dequeue();
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(data->size(), kHandshakeSize);
}

TEST(SendBufferTest, Clear) {
  SendBuffer buffer{};
  
  buffer.Enqueue(Choke{});
  buffer.Enqueue(Unchoke{});
  
  buffer.Clear();
  EXPECT_TRUE(buffer.Empty());
}

TEST(SendBufferTest, Peek) {
  SendBuffer buffer{};
  
  buffer.Enqueue(Choke{});
  
  auto peeked = buffer.Peek();
  EXPECT_FALSE(peeked.empty());
  EXPECT_EQ(buffer.Size(), 1u);  // Still there after peek
}

// ============================================================================
// RequestQueue Tests
// ============================================================================

TEST(RequestQueueTest, AddAndRemove) {
  RequestQueue queue{5};
  
  BlockInfo block{.piece_index = 0, .offset = 0, .length = 16384};
  
  EXPECT_TRUE(queue.Add(block));
  EXPECT_EQ(queue.Size(), 1u);
  EXPECT_TRUE(queue.Contains(block));
  
  EXPECT_TRUE(queue.Remove(block));
  EXPECT_EQ(queue.Size(), 0u);
  EXPECT_FALSE(queue.Contains(block));
}

TEST(RequestQueueTest, MaxPending) {
  RequestQueue queue{2};
  
  BlockInfo block1{.piece_index = 0, .offset = 0, .length = 16384};
  BlockInfo block2{.piece_index = 0, .offset = 16384, .length = 16384};
  BlockInfo block3{.piece_index = 1, .offset = 0, .length = 16384};
  
  EXPECT_TRUE(queue.Add(block1));
  EXPECT_TRUE(queue.Add(block2));
  EXPECT_FALSE(queue.Add(block3));  // Exceeds max
  
  EXPECT_EQ(queue.Size(), 2u);
  EXPECT_FALSE(queue.CanRequest());
}

TEST(RequestQueueTest, GetAll) {
  RequestQueue queue{10};
  
  queue.Add({.piece_index = 0, .offset = 0, .length = 16384});
  queue.Add({.piece_index = 1, .offset = 0, .length = 16384});
  
  auto all = queue.GetAll();
  EXPECT_EQ(all.size(), 2u);
}

TEST(RequestQueueTest, GetTimedOut) {
  RequestQueue queue{10};
  
  queue.Add({.piece_index = 0, .offset = 0, .length = 16384});
  
  // Immediately check - should not be timed out
  auto timed_out = queue.GetTimedOut(std::chrono::seconds{1});
  EXPECT_TRUE(timed_out.empty());
  
  // Can't easily test actual timeout without sleep
}

TEST(RequestQueueTest, Available) {
  RequestQueue queue{5};
  
  EXPECT_EQ(queue.Available(), 5u);
  EXPECT_TRUE(queue.CanRequest());
  
  queue.Add({.piece_index = 0, .offset = 0, .length = 16384});
  queue.Add({.piece_index = 0, .offset = 16384, .length = 16384});
  
  EXPECT_EQ(queue.Available(), 3u);
}

TEST(RequestQueueTest, Clear) {
  RequestQueue queue{10};
  
  queue.Add({.piece_index = 0, .offset = 0, .length = 16384});
  queue.Add({.piece_index = 1, .offset = 0, .length = 16384});
  
  queue.Clear();
  EXPECT_EQ(queue.Size(), 0u);
}

// ============================================================================
// Mock Socket for Connection Tests
// ============================================================================

class MockSocket : public ISocket {
 public:
  void Connect(PeerEndpoint const&,
               std::function<void(std::error_code)> callback) override {
    connect_callback_ = std::move(callback);
    connect_called_ = true;
  }
  
  void Close() override { is_open_ = false; }
  
  [[nodiscard]] bool IsOpen() const override { return is_open_; }
  
  void AsyncRead(std::span<std::byte> buffer,
                 std::function<void(std::error_code, std::size_t)> callback) override {
    read_buffer_ = buffer;
    read_callback_ = std::move(callback);
    read_called_ = true;
  }
  
  void AsyncWrite(std::span<std::byte const> data,
                  std::function<void(std::error_code, std::size_t)> callback) override {
    written_data_.assign(data.begin(), data.end());
    write_callback_ = std::move(callback);
    write_called_ = true;
  }
  
  void SetReadTimeout(std::chrono::seconds) override {}
  void SetWriteTimeout(std::chrono::seconds) override {}
  
  [[nodiscard]] PeerEndpoint GetRemoteEndpoint() const override {
    return {.ip = "127.0.0.1", .port = 6881};
  }
  
  [[nodiscard]] PeerEndpoint GetLocalEndpoint() const override {
    return {.ip = "127.0.0.1", .port = 12345};
  }
  
  // Test helpers
  void SimulateConnectSuccess() {
    is_open_ = true;
    if (connect_callback_) {
      connect_callback_({});
    }
  }
  
  void SimulateConnectFailure(std::errc error) {
    if (connect_callback_) {
      connect_callback_(std::make_error_code(error));
    }
  }
  
  void SimulateReceive(std::span<std::byte const> data) {
    if (read_callback_ && !read_buffer_.empty()) {
      std::size_t to_copy = std::min(data.size(), read_buffer_.size());
      std::copy_n(data.begin(), to_copy, read_buffer_.begin());
      auto cb = std::move(read_callback_);
      read_callback_ = nullptr;
      cb({}, to_copy);
    }
  }
  
  void SimulateWriteComplete() {
    if (write_callback_) {
      auto cb = std::move(write_callback_);
      write_callback_ = nullptr;
      cb({}, written_data_.size());
    }
  }
  
  void SimulateDisconnect() {
    is_open_ = false;
    if (read_callback_) {
      auto cb = std::move(read_callback_);
      read_callback_ = nullptr;
      cb(std::make_error_code(std::errc::connection_reset), 0);
    }
  }
  
  [[nodiscard]] bool ConnectCalled() const { return connect_called_; }
  [[nodiscard]] bool ReadCalled() const { return read_called_; }
  [[nodiscard]] bool WriteCalled() const { return write_called_; }
  [[nodiscard]] std::vector<std::byte> const& GetWrittenData() const {
    return written_data_;
  }
  
  void SetOpen(bool open) { is_open_ = open; }
  
 private:
  bool is_open_{false};
  bool connect_called_{false};
  bool read_called_{false};
  bool write_called_{false};
  
  std::function<void(std::error_code)> connect_callback_;
  std::span<std::byte> read_buffer_;
  std::function<void(std::error_code, std::size_t)> read_callback_;
  std::vector<std::byte> written_data_;
  std::function<void(std::error_code, std::size_t)> write_callback_;
};

// ============================================================================
// Connection Handler for Tests
// ============================================================================

class TestConnectionHandler : public IConnectionHandler {
 public:
  void OnConnected() override { connected_ = true; }
  void OnHandshakeComplete(HandshakeData const& hs) override {
    handshake_complete_ = true;
    last_handshake_ = hs;
  }
  void OnDisconnected(std::error_code ec) override {
    disconnected_ = true;
    disconnect_error_ = ec;
  }
  void OnError(std::error_code ec) override {
    error_occurred_ = true;
    last_error_ = ec;
  }
  void OnMessage(Message const& msg) override {
    messages_.push_back(msg);
  }
  void OnChoke() override { ++choke_count_; }
  void OnUnchoke() override { ++unchoke_count_; }
  void OnInterested() override { ++interested_count_; }
  void OnNotInterested() override { ++not_interested_count_; }
  void OnHave(std::uint32_t idx) override { have_pieces_.push_back(idx); }
  void OnBitfield(Bitfield const& bf) override { last_bitfield_ = bf; }
  void OnPiece(std::uint32_t piece_index, std::uint32_t offset,
               std::span<std::byte const> data) override {
    received_pieces_.push_back({piece_index, offset, data.size()});
  }
  
  void Reset() {
    connected_ = false;
    handshake_complete_ = false;
    disconnected_ = false;
    error_occurred_ = false;
    messages_.clear();
    choke_count_ = 0;
    unchoke_count_ = 0;
    interested_count_ = 0;
    not_interested_count_ = 0;
    have_pieces_.clear();
    received_pieces_.clear();
  }
  
  bool connected_{false};
  bool handshake_complete_{false};
  bool disconnected_{false};
  bool error_occurred_{false};
  std::error_code disconnect_error_{};
  std::error_code last_error_{};
  HandshakeData last_handshake_{};
  std::vector<Message> messages_{};
  int choke_count_{0};
  int unchoke_count_{0};
  int interested_count_{0};
  int not_interested_count_{0};
  std::vector<std::uint32_t> have_pieces_{};
  Bitfield last_bitfield_{};
  
  struct ReceivedPiece {
    std::uint32_t piece_index;
    std::uint32_t offset;
    std::size_t size;
  };
  std::vector<ReceivedPiece> received_pieces_{};
};

// ============================================================================
// PeerConnection Tests
// ============================================================================

class PeerConnectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_socket_ = new MockSocket();  // Connection takes ownership
    
    info_hash_.fill(std::byte{0xAB});
    local_peer_id_ = GeneratePeerId("-BT0001-");
    
    connection_ = CreateConnection(
        std::unique_ptr<ISocket>(mock_socket_),
        info_hash_,
        local_peer_id_,
        100,  // piece_count
        ConnectionConfig{});
    
    connection_->SetHandler(&handler_);
  }
  
  std::vector<std::byte> MakeHandshakeResponse() {
    return MessageCodec::EncodeHandshake(info_hash_, GeneratePeerId("-TR2940-"));
  }
  
  MockSocket* mock_socket_{nullptr};  // Owned by connection
  std::array<std::byte, kInfoHashSize> info_hash_{};
  PeerId local_peer_id_{};
  std::shared_ptr<PeerConnection> connection_{};
  TestConnectionHandler handler_{};
};

TEST_F(PeerConnectionTest, InitialState) {
  EXPECT_EQ(connection_->GetState(), ConnectionState::Disconnected);
  EXPECT_FALSE(connection_->IsConnected());
}

TEST_F(PeerConnectionTest, Connect) {
  PeerEndpoint endpoint{.ip = "192.168.1.1", .port = 6881};
  
  connection_->Connect(endpoint);
  
  EXPECT_TRUE(mock_socket_->ConnectCalled());
  EXPECT_EQ(connection_->GetState(), ConnectionState::Connecting);
}

TEST_F(PeerConnectionTest, ConnectSuccess) {
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  
  EXPECT_EQ(connection_->GetState(), ConnectionState::Handshaking);
  EXPECT_TRUE(mock_socket_->WriteCalled());  // Handshake sent
  
  // Verify handshake was sent
  auto const& written = mock_socket_->GetWrittenData();
  EXPECT_EQ(written.size(), kHandshakeSize);
}

TEST_F(PeerConnectionTest, ConnectFailure) {
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectFailure(std::errc::connection_refused);
  
  EXPECT_EQ(connection_->GetState(), ConnectionState::Error);
  EXPECT_TRUE(handler_.error_occurred_);
}

TEST_F(PeerConnectionTest, HandshakeExchange) {
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  mock_socket_->SimulateWriteComplete();  // Our handshake sent
  
  // Simulate receiving peer's handshake
  auto peer_handshake = MakeHandshakeResponse();
  mock_socket_->SimulateReceive(peer_handshake);
  
  EXPECT_EQ(connection_->GetState(), ConnectionState::Connected);
  EXPECT_TRUE(handler_.connected_);
  EXPECT_TRUE(handler_.handshake_complete_);
}

TEST_F(PeerConnectionTest, HandshakeWrongInfoHash) {
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  mock_socket_->SimulateWriteComplete();
  
  // Create handshake with wrong info_hash
  std::array<std::byte, kInfoHashSize> wrong_hash{};
  wrong_hash.fill(std::byte{0xFF});
  auto bad_handshake = MessageCodec::EncodeHandshake(wrong_hash, GeneratePeerId());
  
  mock_socket_->SimulateReceive(bad_handshake);
  
  EXPECT_EQ(connection_->GetState(), ConnectionState::Error);
}

TEST_F(PeerConnectionTest, SendMessages) {
  // Setup connected state
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  mock_socket_->SimulateWriteComplete();
  mock_socket_->SimulateReceive(MakeHandshakeResponse());
  mock_socket_->SimulateWriteComplete();
  
  // Now send messages
  connection_->SendInterested();
  
  auto const& state = connection_->GetPeerState();
  EXPECT_TRUE(state.am_interested);
}

TEST_F(PeerConnectionTest, PeerState) {
  // Initially both sides choking, no interest
  auto const& state = connection_->GetPeerState();
  EXPECT_TRUE(state.am_choking);
  EXPECT_FALSE(state.am_interested);
  EXPECT_TRUE(state.peer_choking);
  EXPECT_FALSE(state.peer_interested);
}

TEST_F(PeerConnectionTest, RemoteBitfield) {
  // Setup connected
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  mock_socket_->SimulateWriteComplete();
  mock_socket_->SimulateReceive(MakeHandshakeResponse());
  mock_socket_->SimulateWriteComplete();
  
  // Create bitfield message
  Bitfield bf{100};
  bf.SetPiece(10);
  bf.SetPiece(50);
  auto msg = MessageCodec::Encode(bf);
  
  mock_socket_->SimulateReceive(msg);
  
  auto const& remote_bf = connection_->GetRemoteBitfield();
  EXPECT_TRUE(remote_bf.HasPiece(10));
  EXPECT_TRUE(remote_bf.HasPiece(50));
  EXPECT_FALSE(remote_bf.HasPiece(0));
}

TEST_F(PeerConnectionTest, HaveMessage) {
  // Setup connected
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  mock_socket_->SimulateWriteComplete();
  mock_socket_->SimulateReceive(MakeHandshakeResponse());
  mock_socket_->SimulateWriteComplete();
  
  // Receive Have message
  auto msg = MessageCodec::Encode(Have{.piece_index = 42});
  mock_socket_->SimulateReceive(msg);
  
  EXPECT_TRUE(connection_->HasPiece(42));
  EXPECT_EQ(handler_.have_pieces_.size(), 1u);
  EXPECT_EQ(handler_.have_pieces_[0], 42u);
}

TEST_F(PeerConnectionTest, ChokeUnchoke) {
  // Setup connected
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  mock_socket_->SimulateWriteComplete();
  mock_socket_->SimulateReceive(MakeHandshakeResponse());
  mock_socket_->SimulateWriteComplete();
  
  // Receive Unchoke
  mock_socket_->SimulateReceive(MessageCodec::Encode(Unchoke{}));
  EXPECT_FALSE(connection_->GetPeerState().peer_choking);
  EXPECT_EQ(handler_.unchoke_count_, 1);
  
  // Receive Choke
  mock_socket_->SimulateReceive(MessageCodec::Encode(Choke{}));
  EXPECT_TRUE(connection_->GetPeerState().peer_choking);
  EXPECT_EQ(handler_.choke_count_, 1);
}

TEST_F(PeerConnectionTest, RequestTracking) {
  // Setup connected and unchoked
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  mock_socket_->SimulateWriteComplete();
  mock_socket_->SimulateReceive(MakeHandshakeResponse());
  mock_socket_->SimulateWriteComplete();
  mock_socket_->SimulateReceive(MessageCodec::Encode(Unchoke{}));
  
  connection_->SendInterested();
  mock_socket_->SimulateWriteComplete();
  
  // Send request
  BlockInfo block{.piece_index = 5, .offset = 0, .length = 16384};
  connection_->SendRequest(block);
  
  EXPECT_EQ(connection_->PendingRequestCount(), 1u);
  
  auto pending = connection_->GetPendingRequests();
  EXPECT_EQ(pending.size(), 1u);
  EXPECT_EQ(pending[0], block);
}

TEST_F(PeerConnectionTest, PieceReceived) {
  // Setup connected and unchoked
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  mock_socket_->SimulateWriteComplete();
  mock_socket_->SimulateReceive(MakeHandshakeResponse());
  mock_socket_->SimulateWriteComplete();
  mock_socket_->SimulateReceive(MessageCodec::Encode(Unchoke{}));
  
  connection_->SendInterested();
  mock_socket_->SimulateWriteComplete();
  
  // Send request (use small block to fit within single read buffer)
  constexpr std::uint32_t kTestBlockSize = 1024;
  BlockInfo block{.piece_index = 5, .offset = 0, .length = kTestBlockSize};
  connection_->SendRequest(block);
  mock_socket_->SimulateWriteComplete();
  
  // Receive piece
  Piece piece{.piece_index = 5, .offset = 0};
  piece.data.resize(kTestBlockSize, std::byte{0xAB});
  mock_socket_->SimulateReceive(MessageCodec::Encode(piece));
  
  EXPECT_EQ(connection_->PendingRequestCount(), 0u);  // Request fulfilled
  EXPECT_EQ(handler_.received_pieces_.size(), 1u);
  EXPECT_EQ(handler_.received_pieces_[0].piece_index, 5u);
}

TEST_F(PeerConnectionTest, Close) {
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  
  connection_->Close();
  
  EXPECT_EQ(connection_->GetState(), ConnectionState::Disconnected);
  EXPECT_TRUE(handler_.disconnected_);
}

TEST_F(PeerConnectionTest, Stats) {
  auto const& stats = connection_->GetStats();
  EXPECT_EQ(stats.bytes_downloaded, 0u);
  EXPECT_EQ(stats.bytes_uploaded, 0u);
  EXPECT_EQ(stats.messages_received, 0u);
  EXPECT_EQ(stats.messages_sent, 0u);
}

TEST_F(PeerConnectionTest, CanRequest) {
  // Not connected - cannot request
  EXPECT_FALSE(connection_->CanRequest());
  
  // Setup connected
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  mock_socket_->SimulateWriteComplete();
  mock_socket_->SimulateReceive(MakeHandshakeResponse());
  
  // Connected but choked - cannot request
  EXPECT_FALSE(connection_->CanRequest());
  
  // Unchoked but not interested - cannot request
  mock_socket_->SimulateReceive(MessageCodec::Encode(Unchoke{}));
  EXPECT_FALSE(connection_->CanRequest());
  
  // Interested and unchoked - can request
  connection_->SendInterested();
  EXPECT_TRUE(connection_->CanRequest());
}

TEST_F(PeerConnectionTest, AvailableRequestSlots) {
  // Setup connected, unchoked, interested
  connection_->Connect({.ip = "192.168.1.1", .port = 6881});
  mock_socket_->SimulateConnectSuccess();
  mock_socket_->SimulateWriteComplete();
  mock_socket_->SimulateReceive(MakeHandshakeResponse());
  mock_socket_->SimulateReceive(MessageCodec::Encode(Unchoke{}));
  connection_->SendInterested();
  mock_socket_->SimulateWriteComplete();
  
  // Default max is 5
  EXPECT_EQ(connection_->AvailableRequestSlots(), 5u);
  
  connection_->SendRequest({.piece_index = 0, .offset = 0, .length = 16384});
  EXPECT_EQ(connection_->AvailableRequestSlots(), 4u);
}

// ============================================================================
// Connection Config Tests
// ============================================================================

TEST(ConnectionConfigTest, DefaultValues) {
  ConnectionConfig config{};
  
  EXPECT_EQ(config.connect_timeout.count(), 30);
  EXPECT_EQ(config.handshake_timeout.count(), 30);
  EXPECT_EQ(config.request_timeout.count(), 60);
  EXPECT_EQ(config.keep_alive_interval.count(), 120);
  EXPECT_EQ(config.max_pending_requests, 5u);
  EXPECT_FALSE(config.enable_fast_extension);
  EXPECT_FALSE(config.enable_extension_protocol);
}
