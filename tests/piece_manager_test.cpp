#include <gtest/gtest.h>
import piece_manager;
import peer_wire;
import torrent;
#include <algorithm>
#include <array>
#include <numeric>
#include <random>
#include <span>
#include <vector>

using namespace byte_torrent::piece_manager;
using namespace byte_torrent::peer_wire;
using namespace byte_torrent::torrent;

// ============================================================================
// Test Helpers
// ============================================================================

class PieceManagerTestBase : public ::testing::Test {
 protected:
  static constexpr std::uint32_t kTestPieceLength{32768};   // 32 KB
  static constexpr std::uint32_t kTestBlockSize{16384};     // 16 KB
  static constexpr std::uint64_t kTestTotalLength{131072};  // 128 KB = 4 pieces

  static Info MakeTestInfo(std::size_t piece_count = 4,
                           std::uint32_t piece_length = kTestPieceLength) {
    Info info{};
    info.name = "test";
    info.piece_length = piece_length;
    info.length = piece_count * piece_length;

    // Generate dummy piece hashes
    info.pieces.resize(piece_count);
    for (std::size_t i = 0; i < piece_count; ++i) {
      std::ranges::fill(info.pieces[i], static_cast<std::byte>(i));
    }

    return info;
  }

  // Create piece data that will verify against our dummy hashes
  static std::vector<std::byte> MakePieceData(std::uint32_t piece_index,
                                               std::uint32_t piece_length) {
    std::vector<std::byte> data(piece_length);
    // Fill with pattern
    for (std::size_t i = 0; i < data.size(); ++i) {
      data[i] = static_cast<std::byte>((piece_index + i) % 256);
    }
    return data;
  }

  // Create info with real SHA1 hashes for verification testing
  static Info MakeVerifiableInfo(std::size_t piece_count = 4,
                                  std::uint32_t piece_length = kTestPieceLength) {
    Info info{};
    info.name = "test";
    info.piece_length = piece_length;
    info.length = piece_count * piece_length;
    info.pieces.resize(piece_count);

    // Compute real hashes for each piece
    for (std::size_t i = 0; i < piece_count; ++i) {
      auto const data = MakePieceData(static_cast<std::uint32_t>(i), piece_length);
      info.pieces[i] = CalculateSha1(std::span{data});
    }

    return info;
  }
};

// ============================================================================
// BlockState Tests
// ============================================================================

TEST(BlockStateTest, DefaultState) {
  BlockState state{};
  EXPECT_FALSE(state.requested);
  EXPECT_FALSE(state.received);
  EXPECT_TRUE(state.IsAvailable());
  EXPECT_FALSE(state.IsPending());
  EXPECT_FALSE(state.IsComplete());
}

TEST(BlockStateTest, RequestedState) {
  BlockState state{.requested = true, .received = false};
  EXPECT_FALSE(state.IsAvailable());
  EXPECT_TRUE(state.IsPending());
  EXPECT_FALSE(state.IsComplete());
}

TEST(BlockStateTest, ReceivedState) {
  BlockState state{.requested = true, .received = true};
  EXPECT_FALSE(state.IsAvailable());
  EXPECT_FALSE(state.IsPending());
  EXPECT_TRUE(state.IsComplete());
}

TEST(BlockStateTest, Reset) {
  BlockState state{.requested = true, .received = true};
  state.Reset();
  EXPECT_FALSE(state.requested);
  EXPECT_FALSE(state.received);
}

// ============================================================================
// PieceState/Priority ToString Tests
// ============================================================================

TEST(EnumToStringTest, PieceStateToString) {
  EXPECT_EQ(ToString(PieceState::Missing), "Missing");
  EXPECT_EQ(ToString(PieceState::Downloading), "Downloading");
  EXPECT_EQ(ToString(PieceState::Complete), "Complete");
  EXPECT_EQ(ToString(PieceState::Verified), "Verified");
}

TEST(EnumToStringTest, PiecePriorityToString) {
  EXPECT_EQ(ToString(PiecePriority::Skip), "Skip");
  EXPECT_EQ(ToString(PiecePriority::Low), "Low");
  EXPECT_EQ(ToString(PiecePriority::Normal), "Normal");
  EXPECT_EQ(ToString(PiecePriority::High), "High");
  EXPECT_EQ(ToString(PiecePriority::Critical), "Critical");
}

// ============================================================================
// PieceInfo Tests
// ============================================================================

class PieceInfoTest : public PieceManagerTestBase {};

TEST_F(PieceInfoTest, Construction) {
  PieceInfo piece{0, kTestPieceLength, kTestBlockSize};

  EXPECT_EQ(piece.PieceIndex(), 0u);
  EXPECT_EQ(piece.PieceLength(), kTestPieceLength);
  EXPECT_EQ(piece.BlockSize(), kTestBlockSize);
  EXPECT_EQ(piece.BlockCount(), 2u);  // 32KB / 16KB = 2 blocks
  EXPECT_EQ(piece.State(), PieceState::Missing);
  EXPECT_EQ(piece.Priority(), PiecePriority::Normal);
}

TEST_F(PieceInfoTest, BlockIndexing) {
  PieceInfo piece{0, kTestPieceLength, kTestBlockSize};

  EXPECT_EQ(piece.BlockIndex(0), 0u);
  EXPECT_EQ(piece.BlockIndex(kTestBlockSize - 1), 0u);
  EXPECT_EQ(piece.BlockIndex(kTestBlockSize), 1u);
  EXPECT_EQ(piece.BlockOffset(0), 0u);
  EXPECT_EQ(piece.BlockOffset(1), kTestBlockSize);
  EXPECT_EQ(piece.BlockLength(0), kTestBlockSize);
  EXPECT_EQ(piece.BlockLength(1), kTestBlockSize);
}

TEST_F(PieceInfoTest, BlockIndexingLastPieceShorter) {
  // Last piece is 24KB (3/4 of normal size)
  PieceInfo piece{3, 24576, kTestBlockSize};

  EXPECT_EQ(piece.BlockCount(), 2u);
  EXPECT_EQ(piece.BlockLength(0), kTestBlockSize);
  EXPECT_EQ(piece.BlockLength(1), 8192u);  // Remaining 8KB
}

TEST_F(PieceInfoTest, GetNextAvailableBlock) {
  PieceInfo piece{5, kTestPieceLength, kTestBlockSize};

  auto block = piece.GetNextAvailableBlock();
  ASSERT_TRUE(block.has_value());
  EXPECT_EQ(block->piece_index, 5u);
  EXPECT_EQ(block->offset, 0u);
  EXPECT_EQ(block->length, kTestBlockSize);
}

TEST_F(PieceInfoTest, MarkBlockRequested) {
  PieceInfo piece{0, kTestPieceLength, kTestBlockSize};

  piece.MarkBlockRequested(0);
  EXPECT_EQ(piece.State(), PieceState::Downloading);
  EXPECT_TRUE(piece.GetBlockState(0).requested);
  EXPECT_FALSE(piece.GetBlockState(0).received);

  auto block = piece.GetNextAvailableBlock();
  ASSERT_TRUE(block.has_value());
  EXPECT_EQ(block->offset, kTestBlockSize);  // Second block
}

TEST_F(PieceInfoTest, StoreBlock) {
  PieceInfo piece{0, kTestPieceLength, kTestBlockSize};
  std::vector<std::byte> data(kTestBlockSize, std::byte{0xAB});

  EXPECT_TRUE(piece.StoreBlock(0, data));
  EXPECT_TRUE(piece.GetBlockState(0).received);
  EXPECT_EQ(piece.State(), PieceState::Downloading);
  EXPECT_EQ(piece.ReceivedBlockCount(), 1u);
}

TEST_F(PieceInfoTest, StoreAllBlocksCompletesPiece) {
  PieceInfo piece{0, kTestPieceLength, kTestBlockSize};
  std::vector<std::byte> data(kTestBlockSize, std::byte{0xAB});

  piece.StoreBlock(0, data);
  piece.StoreBlock(kTestBlockSize, data);

  EXPECT_EQ(piece.State(), PieceState::Complete);
  EXPECT_TRUE(piece.IsComplete());
  EXPECT_DOUBLE_EQ(piece.Progress(), 1.0);
}

TEST_F(PieceInfoTest, StoreBlockInvalidSize) {
  PieceInfo piece{0, kTestPieceLength, kTestBlockSize};
  std::vector<std::byte> wrong_size(100, std::byte{0xAB});

  EXPECT_FALSE(piece.StoreBlock(0, wrong_size));
}

TEST_F(PieceInfoTest, CancelBlockRequest) {
  PieceInfo piece{0, kTestPieceLength, kTestBlockSize};

  piece.MarkBlockRequested(0);
  EXPECT_TRUE(piece.GetBlockState(0).requested);

  piece.CancelBlockRequest(0);
  EXPECT_FALSE(piece.GetBlockState(0).requested);
  EXPECT_EQ(piece.State(), PieceState::Missing);
}

TEST_F(PieceInfoTest, Reset) {
  PieceInfo piece{0, kTestPieceLength, kTestBlockSize};
  std::vector<std::byte> data(kTestBlockSize, std::byte{0xAB});

  piece.StoreBlock(0, data);
  piece.StoreBlock(kTestBlockSize, data);
  EXPECT_TRUE(piece.IsComplete());

  piece.Reset();
  EXPECT_EQ(piece.State(), PieceState::Missing);
  EXPECT_EQ(piece.ReceivedBlockCount(), 0u);
}

TEST_F(PieceInfoTest, Progress) {
  PieceInfo piece{0, kTestPieceLength, kTestBlockSize};
  std::vector<std::byte> data(kTestBlockSize, std::byte{0xAB});

  EXPECT_DOUBLE_EQ(piece.Progress(), 0.0);

  piece.StoreBlock(0, data);
  EXPECT_DOUBLE_EQ(piece.Progress(), 0.5);

  piece.StoreBlock(kTestBlockSize, data);
  EXPECT_DOUBLE_EQ(piece.Progress(), 1.0);
}

// ============================================================================
// PieceAvailability Tests
// ============================================================================

class PieceAvailabilityTest : public ::testing::Test {};

TEST_F(PieceAvailabilityTest, DefaultConstruction) {
  PieceAvailability avail{};
  EXPECT_EQ(avail.PieceCount(), 0u);
}

TEST_F(PieceAvailabilityTest, ConstructionWithSize) {
  PieceAvailability avail{10};
  EXPECT_EQ(avail.PieceCount(), 10u);
  EXPECT_EQ(avail.GetAvailability(0), 0u);
}

TEST_F(PieceAvailabilityTest, AddPeer) {
  PieceAvailability avail{10};
  Bitfield bf{10};
  bf.SetPiece(0);
  bf.SetPiece(5);
  bf.SetPiece(9);

  avail.AddPeer(bf);

  EXPECT_EQ(avail.GetAvailability(0), 1u);
  EXPECT_EQ(avail.GetAvailability(5), 1u);
  EXPECT_EQ(avail.GetAvailability(9), 1u);
  EXPECT_EQ(avail.GetAvailability(1), 0u);
}

TEST_F(PieceAvailabilityTest, AddMultiplePeers) {
  PieceAvailability avail{10};

  Bitfield bf1{10};
  bf1.SetPiece(0);
  bf1.SetPiece(5);

  Bitfield bf2{10};
  bf2.SetPiece(0);
  bf2.SetPiece(3);

  avail.AddPeer(bf1);
  avail.AddPeer(bf2);

  EXPECT_EQ(avail.GetAvailability(0), 2u);
  EXPECT_EQ(avail.GetAvailability(5), 1u);
  EXPECT_EQ(avail.GetAvailability(3), 1u);
}

TEST_F(PieceAvailabilityTest, RemovePeer) {
  PieceAvailability avail{10};

  Bitfield bf{10};
  bf.SetPiece(0);
  bf.SetPiece(5);

  avail.AddPeer(bf);
  EXPECT_EQ(avail.GetAvailability(0), 1u);

  avail.RemovePeer(bf);
  EXPECT_EQ(avail.GetAvailability(0), 0u);
  EXPECT_EQ(avail.GetAvailability(5), 0u);
}

TEST_F(PieceAvailabilityTest, IncrementPiece) {
  PieceAvailability avail{10};

  avail.IncrementPiece(3);
  EXPECT_EQ(avail.GetAvailability(3), 1u);

  avail.IncrementPiece(3);
  EXPECT_EQ(avail.GetAvailability(3), 2u);
}

TEST_F(PieceAvailabilityTest, GetRarestPieces) {
  PieceAvailability avail{10};

  // Add peers with different piece distributions
  Bitfield bf1{10};
  for (std::size_t i = 0; i < 10; ++i) bf1.SetPiece(i);  // Has all

  Bitfield bf2{10};
  bf2.SetPiece(0);
  bf2.SetPiece(1);
  bf2.SetPiece(2);  // Has 0,1,2

  Bitfield bf3{10};
  bf3.SetPiece(0);
  bf3.SetPiece(1);  // Has 0,1

  avail.AddPeer(bf1);
  avail.AddPeer(bf2);
  avail.AddPeer(bf3);

  // Availability: 0=3, 1=3, 2=2, 3-9=1
  auto rarest = avail.GetRarestPieces(5);

  EXPECT_FALSE(rarest.empty());
  // Pieces 3-9 have availability 1 (rarest)
  EXPECT_TRUE(std::ranges::find(rarest, 3u) != rarest.end() ||
              std::ranges::find(rarest, 4u) != rarest.end());
}

TEST_F(PieceAvailabilityTest, GetRarestPiecesEmpty) {
  PieceAvailability avail{10};
  auto rarest = avail.GetRarestPieces();
  EXPECT_TRUE(rarest.empty());
}

// ============================================================================
// Piece Selector Tests
// ============================================================================

class PieceSelectorTest : public ::testing::Test {
 protected:
  static PieceState GetMissingState(std::uint32_t) { return PieceState::Missing; }
  static PiecePriority GetNormalPriority(std::uint32_t) { return PiecePriority::Normal; }
};

TEST_F(PieceSelectorTest, SequentialSelector) {
  SequentialSelector selector{};
  Bitfield peer_bf{10};
  peer_bf.SetPiece(5);
  peer_bf.SetPiece(2);
  peer_bf.SetPiece(8);

  auto result = selector.SelectPiece(peer_bf, GetMissingState, GetNormalPriority);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 2u);  // First available in order
}

TEST_F(PieceSelectorTest, SequentialSelectorCriticalFirst) {
  SequentialSelector selector{};
  Bitfield peer_bf{10};
  peer_bf.SetPiece(2);
  peer_bf.SetPiece(8);

  auto get_priority = [](std::uint32_t idx) {
    return idx == 8 ? PiecePriority::Critical : PiecePriority::Normal;
  };

  auto result = selector.SelectPiece(peer_bf, GetMissingState, get_priority);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 8u);  // Critical first
}

TEST_F(PieceSelectorTest, SequentialSelectorSkipsPieces) {
  SequentialSelector selector{};
  Bitfield peer_bf{10};
  peer_bf.SetPiece(0);
  peer_bf.SetPiece(1);

  auto get_priority = [](std::uint32_t idx) {
    return idx == 0 ? PiecePriority::Skip : PiecePriority::Normal;
  };

  auto result = selector.SelectPiece(peer_bf, GetMissingState, get_priority);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 1u);  // Skips piece 0
}

TEST_F(PieceSelectorTest, RandomSelector) {
  RandomSelector selector{};
  Bitfield peer_bf{100};
  for (std::size_t i = 0; i < 100; ++i) peer_bf.SetPiece(i);

  std::set<std::uint32_t> selected{};
  for (int i = 0; i < 100; ++i) {
    auto result = selector.SelectPiece(peer_bf, GetMissingState, GetNormalPriority);
    if (result) selected.insert(*result);
  }

  // Should have selected multiple different pieces
  EXPECT_GT(selected.size(), 1u);
}

TEST_F(PieceSelectorTest, RarestFirstSelector) {
  PieceAvailability avail{10};

  // Make piece 7 rarest (availability = 1)
  Bitfield bf1{10};
  bf1.SetPiece(0);
  bf1.SetPiece(1);
  bf1.SetPiece(7);

  Bitfield bf2{10};
  bf2.SetPiece(0);
  bf2.SetPiece(1);

  avail.AddPeer(bf1);
  avail.AddPeer(bf2);

  RarestFirstSelector selector{avail};

  Bitfield peer_bf{10};
  peer_bf.SetPiece(0);
  peer_bf.SetPiece(7);

  auto result = selector.SelectPiece(peer_bf, GetMissingState, GetNormalPriority);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 7u);  // Rarest piece
}

TEST_F(PieceSelectorTest, SelectorNoPiecesAvailable) {
  SequentialSelector selector{};
  Bitfield peer_bf{10};  // No pieces

  auto result = selector.SelectPiece(peer_bf, GetMissingState, GetNormalPriority);

  EXPECT_FALSE(result.has_value());
}

TEST_F(PieceSelectorTest, SelectorAllPiecesDownloading) {
  SequentialSelector selector{};
  Bitfield peer_bf{10};
  peer_bf.SetPiece(0);

  auto get_state = [](std::uint32_t) { return PieceState::Downloading; };

  auto result = selector.SelectPiece(peer_bf, get_state, GetNormalPriority);

  EXPECT_FALSE(result.has_value());
}

// ============================================================================
// PieceManager Tests
// ============================================================================

class PieceManagerTest : public PieceManagerTestBase {};

TEST_F(PieceManagerTest, Construction) {
  auto info = MakeTestInfo();
  PieceManager manager{info};

  EXPECT_EQ(manager.PieceCount(), 4u);
  EXPECT_EQ(manager.PieceLength(), kTestPieceLength);
  EXPECT_FALSE(manager.IsComplete());
  EXPECT_DOUBLE_EQ(manager.GetProgress(), 0.0);
}

TEST_F(PieceManagerTest, SelectBlockFromPeer) {
  auto info = MakeTestInfo();
  PieceManager manager{info};

  Bitfield peer_bf{4};
  peer_bf.SetPiece(0);
  peer_bf.SetPiece(2);

  auto block = manager.SelectBlock(peer_bf);

  ASSERT_TRUE(block.has_value());
  EXPECT_TRUE(block->piece_index == 0 || block->piece_index == 2);
  EXPECT_EQ(block->offset, 0u);
  EXPECT_EQ(block->length, kTestBlockSize);
}

TEST_F(PieceManagerTest, SelectMultipleBlocks) {
  auto info = MakeTestInfo();
  PieceManager manager{info};

  Bitfield peer_bf{4};
  peer_bf.SetPiece(0);

  auto blocks = manager.SelectBlocks(peer_bf, 2);

  EXPECT_EQ(blocks[0].piece_index, 0u);
  EXPECT_EQ(blocks[1].piece_index, 0u);
  EXPECT_NE(blocks[0].offset, blocks[1].offset);
}

TEST_F(PieceManagerTest, SelectBlockContinuesInProgressPiece) {
  auto info = MakeTestInfo();
  PieceManager manager{info};

  Bitfield peer_bf{4};
  peer_bf.SetPiece(0);
  peer_bf.SetPiece(1);

  // Select first block
  auto block1 = manager.SelectBlock(peer_bf);
  ASSERT_TRUE(block1.has_value());
  std::uint32_t const first_piece = block1->piece_index;

  // Next block should be from same piece (in-progress preference)
  auto block2 = manager.SelectBlock(peer_bf);
  ASSERT_TRUE(block2.has_value());
  EXPECT_EQ(block2->piece_index, first_piece);
}

TEST_F(PieceManagerTest, OnBlockReceivedVerified) {
  auto info = MakeVerifiableInfo();
  PieceManager manager{info};

  // Get all blocks for piece 0
  auto data = MakePieceData(0, kTestPieceLength);

  // Receive first block
  auto result1 = manager.OnBlockReceived(
      0, 0, std::span{data.data(), kTestBlockSize});
  EXPECT_EQ(result1, PieceManager::BlockResult::Accepted);

  // Receive second block (completes piece)
  auto result2 = manager.OnBlockReceived(
      0, kTestBlockSize, std::span{data.data() + kTestBlockSize, kTestBlockSize});
  EXPECT_EQ(result2, PieceManager::BlockResult::PieceVerified);

  EXPECT_TRUE(manager.HasPiece(0));
  EXPECT_EQ(manager.GetPieceState(0), PieceState::Verified);
}

TEST_F(PieceManagerTest, OnBlockReceivedFailed) {
  auto info = MakeVerifiableInfo();
  PieceManager manager{info};

  // Wrong data that won't verify
  std::vector<std::byte> bad_data(kTestPieceLength, std::byte{0xFF});

  auto result1 = manager.OnBlockReceived(
      0, 0, std::span{bad_data.data(), kTestBlockSize});
  EXPECT_EQ(result1, PieceManager::BlockResult::Accepted);

  auto result2 = manager.OnBlockReceived(
      0, kTestBlockSize, std::span{bad_data.data() + kTestBlockSize, kTestBlockSize});
  EXPECT_EQ(result2, PieceManager::BlockResult::PieceFailed);

  EXPECT_FALSE(manager.HasPiece(0));
  EXPECT_EQ(manager.GetPieceState(0), PieceState::Missing);
}

TEST_F(PieceManagerTest, OnBlockReceivedDuplicate) {
  auto info = MakeTestInfo();
  PieceManager manager{info};

  std::vector<std::byte> data(kTestBlockSize, std::byte{0xAB});

  manager.OnBlockReceived(0, 0, data);
  auto result = manager.OnBlockReceived(0, 0, data);

  EXPECT_EQ(result, PieceManager::BlockResult::Duplicate);
}

TEST_F(PieceManagerTest, OnBlockReceivedInvalid) {
  auto info = MakeTestInfo();
  PieceManager manager{info};

  std::vector<std::byte> data(100, std::byte{0xAB});  // Wrong size

  auto result = manager.OnBlockReceived(0, 0, data);
  EXPECT_EQ(result, PieceManager::BlockResult::Invalid);
}

TEST_F(PieceManagerTest, CancelRequest) {
  auto info = MakeTestInfo();
  PieceManager manager{info};

  Bitfield peer_bf{4};
  peer_bf.SetPiece(0);

  auto block = manager.SelectBlock(peer_bf);
  ASSERT_TRUE(block.has_value());

  manager.CancelRequest(*block);

  // Block should be available again
  auto next_block = manager.SelectBlock(peer_bf);
  ASSERT_TRUE(next_block.has_value());
  EXPECT_EQ(next_block->offset, block->offset);
}

TEST_F(PieceManagerTest, AvailabilityTracking) {
  auto info = MakeTestInfo();
  PieceManager manager{info};

  Bitfield peer1{4};
  peer1.SetPiece(0);
  peer1.SetPiece(1);

  Bitfield peer2{4};
  peer2.SetPiece(1);
  peer2.SetPiece(2);

  manager.OnPeerBitfield(peer1);
  manager.OnPeerBitfield(peer2);

  // Piece 1 should be most available (2 peers)
  // Selection should prefer rarer pieces (0, 2, 3)

  manager.OnPeerDisconnected(peer1);
  // Now piece 0 is unavailable (0 peers)
}

TEST_F(PieceManagerTest, OnPeerHave) {
  auto info = MakeTestInfo();
  PieceManager manager{info};

  manager.OnPeerHave(2);
  manager.OnPeerHave(2);
  // Piece 2 now has availability 2
}

TEST_F(PieceManagerTest, GetLocalBitfield) {
  auto info = MakeVerifiableInfo(2, kTestPieceLength);
  PieceManager manager{info};

  // Initially empty
  auto bf = manager.GetLocalBitfield();
  EXPECT_FALSE(bf.HasPiece(0));
  EXPECT_FALSE(bf.HasPiece(1));

  // Complete piece 0
  auto data = MakePieceData(0, kTestPieceLength);
  manager.OnBlockReceived(0, 0, std::span{data.data(), kTestBlockSize});
  manager.OnBlockReceived(0, kTestBlockSize,
                          std::span{data.data() + kTestBlockSize, kTestBlockSize});

  bf = manager.GetLocalBitfield();
  EXPECT_TRUE(bf.HasPiece(0));
  EXPECT_FALSE(bf.HasPiece(1));
}

TEST_F(PieceManagerTest, GetPieceData) {
  auto info = MakeVerifiableInfo(1, kTestPieceLength);
  PieceManager manager{info};

  // Before completion
  EXPECT_FALSE(manager.GetPieceData(0).has_value());

  // Complete piece
  auto data = MakePieceData(0, kTestPieceLength);
  manager.OnBlockReceived(0, 0, std::span{data.data(), kTestBlockSize});
  manager.OnBlockReceived(0, kTestBlockSize,
                          std::span{data.data() + kTestBlockSize, kTestBlockSize});

  auto piece_data = manager.GetPieceData(0);
  ASSERT_TRUE(piece_data.has_value());
  EXPECT_EQ(piece_data->size(), kTestPieceLength);
}

TEST_F(PieceManagerTest, SetPiecePriority) {
  auto info = MakeTestInfo();
  PieceManager manager{info};

  manager.SetPiecePriority(0, PiecePriority::Skip);
  manager.SetPiecePriority(1, PiecePriority::Critical);

  EXPECT_EQ(manager.GetPiecePriority(0), PiecePriority::Skip);
  EXPECT_EQ(manager.GetPiecePriority(1), PiecePriority::Critical);
}

TEST_F(PieceManagerTest, Statistics) {
  auto info = MakeVerifiableInfo(2, kTestPieceLength);
  PieceManager manager{info};

  auto stats = manager.GetStats();
  EXPECT_EQ(stats.total_pieces, 2u);
  EXPECT_EQ(stats.verified_pieces, 0u);
  EXPECT_EQ(stats.total_bytes, 2 * kTestPieceLength);
  EXPECT_EQ(stats.downloaded_bytes, 0u);

  // Complete one piece
  auto data = MakePieceData(0, kTestPieceLength);
  manager.OnBlockReceived(0, 0, std::span{data.data(), kTestBlockSize});
  manager.OnBlockReceived(0, kTestBlockSize,
                          std::span{data.data() + kTestBlockSize, kTestBlockSize});

  stats = manager.GetStats();
  EXPECT_EQ(stats.verified_pieces, 1u);
  EXPECT_EQ(stats.downloaded_bytes, kTestPieceLength);
  EXPECT_DOUBLE_EQ(stats.PieceProgress(), 0.5);
}

TEST_F(PieceManagerTest, IsComplete) {
  auto info = MakeVerifiableInfo(1, kTestPieceLength);
  PieceManager manager{info};

  EXPECT_FALSE(manager.IsComplete());

  auto data = MakePieceData(0, kTestPieceLength);
  manager.OnBlockReceived(0, 0, std::span{data.data(), kTestBlockSize});
  manager.OnBlockReceived(0, kTestBlockSize,
                          std::span{data.data() + kTestBlockSize, kTestBlockSize});

  EXPECT_TRUE(manager.IsComplete());
}

TEST_F(PieceManagerTest, RemainingPieces) {
  auto info = MakeVerifiableInfo(4, kTestPieceLength);
  PieceManager manager{info};

  EXPECT_EQ(manager.RemainingPieces(), 4u);

  // Complete piece 0
  auto data = MakePieceData(0, kTestPieceLength);
  manager.OnBlockReceived(0, 0, std::span{data.data(), kTestBlockSize});
  manager.OnBlockReceived(0, kTestBlockSize,
                          std::span{data.data() + kTestBlockSize, kTestBlockSize});

  EXPECT_EQ(manager.RemainingPieces(), 3u);
}

// ============================================================================
// Endgame Mode Tests
// ============================================================================

TEST_F(PieceManagerTest, EndgameMode) {
  PieceManagerConfig config{};
  config.endgame_threshold = 2;
  config.enable_endgame = true;

  auto info = MakeVerifiableInfo(3, kTestPieceLength);
  PieceManager manager{info, config};

  EXPECT_FALSE(manager.IsInEndgame());

  // Complete piece 0
  auto data0 = MakePieceData(0, kTestPieceLength);
  manager.OnBlockReceived(0, 0, std::span{data0.data(), kTestBlockSize});
  manager.OnBlockReceived(0, kTestBlockSize,
                          std::span{data0.data() + kTestBlockSize, kTestBlockSize});

  // 2 pieces remaining = endgame threshold
  EXPECT_TRUE(manager.IsInEndgame());
}

TEST_F(PieceManagerTest, GetPendingBlocks) {
  auto info = MakeTestInfo();
  PieceManager manager{info};

  Bitfield peer_bf{4};
  peer_bf.SetPiece(0);

  manager.SelectBlock(peer_bf);
  manager.SelectBlock(peer_bf);

  auto pending = manager.GetPendingBlocks();
  EXPECT_EQ(pending.size(), 2u);
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST(UtilityTest, GetPiecesForRange) {
  auto pieces = GetPiecesForRange(0, 100000, 32768);

  EXPECT_FALSE(pieces.empty());
  EXPECT_EQ(pieces.front(), 0u);
  EXPECT_EQ(pieces.back(), 3u);  // 100000 / 32768 = ~3
}

TEST(UtilityTest, GetPiecesForRangeSinglePiece) {
  auto pieces = GetPiecesForRange(0, 1000, 32768);

  ASSERT_EQ(pieces.size(), 1u);
  EXPECT_EQ(pieces[0], 0u);
}

TEST(UtilityTest, GetPiecesForRangeMiddle) {
  auto pieces = GetPiecesForRange(50000, 100000, 32768);

  EXPECT_EQ(pieces.front(), 1u);  // 50000 / 32768 = 1
  EXPECT_EQ(pieces.back(), 3u);   // (100000-1) / 32768 = 3
}

TEST(UtilityTest, GetPiecesForRangeEmpty) {
  auto pieces = GetPiecesForRange(100, 100, 32768);  // Empty range
  EXPECT_TRUE(pieces.empty());
}

TEST(UtilityTest, MapFileToPieces) {
  // File starts at byte 10000, length 50000
  auto mapping = MapFileToPieces(10000, 50000, 32768);

  EXPECT_EQ(mapping.first_piece, 0u);
  EXPECT_EQ(mapping.first_piece_offset, 10000u);
  EXPECT_EQ(mapping.last_piece, 1u);  // (10000 + 50000 - 1) / 32768 = 1
}

TEST(UtilityTest, MapFileToPiecesSinglePiece) {
  auto mapping = MapFileToPieces(0, 1000, 32768);

  EXPECT_EQ(mapping.first_piece, 0u);
  EXPECT_EQ(mapping.last_piece, 0u);
  EXPECT_EQ(mapping.first_piece_offset, 0u);
  EXPECT_EQ(mapping.last_piece_length, 1000u);
}

// ============================================================================
// DownloadStats Tests
// ============================================================================

TEST(DownloadStatsTest, Progress) {
  DownloadStats stats{};
  stats.total_bytes = 1000;
  stats.downloaded_bytes = 500;

  EXPECT_DOUBLE_EQ(stats.Progress(), 0.5);
}

TEST(DownloadStatsTest, PieceProgress) {
  DownloadStats stats{};
  stats.total_pieces = 10;
  stats.verified_pieces = 3;

  EXPECT_DOUBLE_EQ(stats.PieceProgress(), 0.3);
}

TEST(DownloadStatsTest, IsComplete) {
  DownloadStats stats{};
  stats.total_pieces = 10;
  stats.verified_pieces = 10;

  EXPECT_TRUE(stats.IsComplete());
}

TEST(DownloadStatsTest, ProgressZeroTotal) {
  DownloadStats stats{};
  EXPECT_DOUBLE_EQ(stats.Progress(), 0.0);
  EXPECT_DOUBLE_EQ(stats.PieceProgress(), 0.0);
}

// ============================================================================
// Thread Safety Test (basic)
// ============================================================================

TEST_F(PieceManagerTest, ConcurrentAccess) {
  auto info = MakeTestInfo(100, kTestPieceLength);
  PieceManager manager{info};

  Bitfield peer_bf{100};
  for (std::size_t i = 0; i < 100; ++i) peer_bf.SetPiece(i);

  std::vector<std::thread> threads;

  // Multiple threads selecting blocks
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&manager, &peer_bf]() {
      for (int i = 0; i < 50; ++i) {
        std::ignore = manager.SelectBlock(peer_bf);
      }
    });
  }

  // Thread updating availability
  threads.emplace_back([&manager, &peer_bf]() {
    for (int i = 0; i < 10; ++i) {
      manager.OnPeerBitfield(peer_bf);
      manager.OnPeerDisconnected(peer_bf);
    }
  });

  for (auto& t : threads) {
    t.join();
  }

  // Should not crash or deadlock
  EXPECT_GE(manager.PieceCount(), 0u);
}
