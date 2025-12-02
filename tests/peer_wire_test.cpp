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
