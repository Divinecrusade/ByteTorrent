#include <gtest/gtest.h>
import peer_manager;
import peer_wire;
import piece_manager;
import torrent;
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

using namespace byte_torrent::peer_manager;
using namespace byte_torrent::peer_wire;
using namespace byte_torrent::piece_manager;
using namespace byte_torrent::torrent;

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

class PeerManagerTestBase : public ::testing::Test {
 protected:
  static constexpr std::uint32_t kTestPieceLength{32768};
  static constexpr std::size_t kTestPieceCount{10};

  void SetUp() override {
    // Create test torrent info
    info_.name = "test_torrent";
    info_.piece_length = kTestPieceLength;
    info_.length = kTestPieceCount * kTestPieceLength;
    info_.pieces.resize(kTestPieceCount);
    for (std::size_t i = 0; i < kTestPieceCount; ++i) {
      std::ranges::fill(info_.pieces[i], static_cast<std::byte>(i));
    }

    torrent_.announce = "http://tracker.example.com/announce";
    torrent_.info = info_;

    piece_manager_ = std::make_unique<PieceManager>(info_);
    local_peer_id_ = GeneratePeerId("-BT0001-");
  }

  Info info_{};
  Torrent torrent_{};
  std::unique_ptr<PieceManager> piece_manager_{};
  PeerId local_peer_id_{};
};

// ============================================================================
// PeerInfo Tests
// ============================================================================

TEST(PeerInfoTest, DefaultConstruction) {
  PeerInfo info{};

  EXPECT_TRUE(info.endpoint.ip.empty());
  EXPECT_EQ(info.endpoint.port, 0);
  EXPECT_FALSE(info.peer_id.has_value());
  EXPECT_FALSE(info.is_seed);
  EXPECT_FALSE(info.is_snubbed);
}

TEST(PeerInfoTest, IsInteresting) {
  PeerInfo info{};
  info.bitfield = Bitfield{10};
  info.bitfield.SetPiece(0);
  info.bitfield.SetPiece(5);

  Bitfield local{10};
  local.SetPiece(0);  // We have piece 0

  // Peer has piece 5 that we don't have
  EXPECT_TRUE(info.IsInteresting(local));

  // Now we have all pieces peer has
  local.SetPiece(5);
  EXPECT_FALSE(info.IsInteresting(local));
}

TEST(PeerInfoTest, IsInterestingSeed) {
  PeerInfo info{};
  info.bitfield = Bitfield{10};
  for (std::size_t i = 0; i < 10; ++i) {
    info.bitfield.SetPiece(i);
  }
  info.is_seed = true;

  Bitfield local{10};  // We have nothing
  EXPECT_TRUE(info.IsInteresting(local));

  // Now we're also a seed
  for (std::size_t i = 0; i < 10; ++i) {
    local.SetPiece(i);
  }
  EXPECT_FALSE(info.IsInteresting(local));
}

// ============================================================================
// PeerManagerConfig Tests
// ============================================================================

TEST(PeerManagerConfigTest, DefaultValues) {
  PeerManagerConfig config{};

  EXPECT_EQ(config.max_connections, 50u);
  EXPECT_EQ(config.max_upload_slots, 4u);
  EXPECT_EQ(config.optimistic_unchoke_slots, 1u);
  EXPECT_EQ(config.choke_interval.count(), 10);
  EXPECT_EQ(config.optimistic_unchoke_interval.count(), 30);
  EXPECT_EQ(config.request_timeout.count(), 60);
  EXPECT_EQ(config.max_requests_per_peer, 5u);
  EXPECT_TRUE(config.enable_endgame);
}

// ============================================================================
// PeerManager Basic Tests
// ============================================================================

class PeerManagerTest : public PeerManagerTestBase {};

TEST_F(PeerManagerTest, Construction) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};

  EXPECT_FALSE(manager.IsRunning());
  EXPECT_EQ(manager.ConnectionCount(), 0u);
  EXPECT_EQ(manager.PendingConnectionCount(), 0u);
}

TEST_F(PeerManagerTest, StartStop) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};

  manager.Start();
  EXPECT_TRUE(manager.IsRunning());

  manager.Stop();
  EXPECT_FALSE(manager.IsRunning());
}

TEST_F(PeerManagerTest, AddPeerWhenNotRunning) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};

  PeerEndpoint endpoint{.ip = "192.168.1.1", .port = 6881};
  EXPECT_FALSE(manager.AddPeer(endpoint));
}

TEST_F(PeerManagerTest, AddPeerWhenRunning) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};
  manager.Start();

  PeerEndpoint endpoint{.ip = "192.168.1.1", .port = 6881};
  EXPECT_TRUE(manager.AddPeer(endpoint));
  EXPECT_EQ(manager.PendingConnectionCount(), 1u);
}

TEST_F(PeerManagerTest, AddDuplicatePeer) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};
  manager.Start();

  PeerEndpoint endpoint{.ip = "192.168.1.1", .port = 6881};
  EXPECT_TRUE(manager.AddPeer(endpoint));
  EXPECT_TRUE(manager.AddPeer(endpoint));  // Duplicates go to pending
  EXPECT_EQ(manager.PendingConnectionCount(), 2u);
}

TEST_F(PeerManagerTest, AddMultiplePeers) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};
  manager.Start();

  std::vector<PeerEndpoint> endpoints{
      {.ip = "192.168.1.1", .port = 6881},
      {.ip = "192.168.1.2", .port = 6881},
      {.ip = "192.168.1.3", .port = 6881},
  };

  std::size_t added = manager.AddPeers(endpoints);
  EXPECT_EQ(added, 3u);
  EXPECT_EQ(manager.PendingConnectionCount(), 3u);
}

TEST_F(PeerManagerTest, MaxConnections) {
  PeerManagerConfig config{};
  config.max_connections = 2;

  PeerManager manager{torrent_, *piece_manager_, local_peer_id_, config};
  manager.Start();

  // Add more than max
  for (int i = 0; i < 5; ++i) {
    manager.AddPeer({.ip = "192.168.1." + std::to_string(i), .port = 6881});
  }

  // All go to pending (actual connections happen externally)
  EXPECT_EQ(manager.PendingConnectionCount(), 5u);
}

TEST_F(PeerManagerTest, GetNextPendingPeer) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};
  manager.Start();

  PeerEndpoint ep1{.ip = "192.168.1.1", .port = 6881};
  PeerEndpoint ep2{.ip = "192.168.1.2", .port = 6882};

  manager.AddPeer(ep1);
  manager.AddPeer(ep2);

  auto next1 = manager.GetNextPendingPeer();
  ASSERT_TRUE(next1.has_value());
  EXPECT_EQ(next1->ip, "192.168.1.1");

  auto next2 = manager.GetNextPendingPeer();
  ASSERT_TRUE(next2.has_value());
  EXPECT_EQ(next2->ip, "192.168.1.2");

  auto next3 = manager.GetNextPendingPeer();
  EXPECT_FALSE(next3.has_value());
}

TEST_F(PeerManagerTest, GetPeerInfos) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};
  manager.Start();

  auto infos = manager.GetPeerInfos();
  EXPECT_TRUE(infos.empty());
}

TEST_F(PeerManagerTest, GetAggregateStats) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};
  manager.Start();

  auto stats = manager.GetAggregateStats();
  EXPECT_EQ(stats.total_downloaded, 0u);
  EXPECT_EQ(stats.total_uploaded, 0u);
  EXPECT_EQ(stats.connected_peers, 0u);
  EXPECT_EQ(stats.seeders, 0u);
  EXPECT_EQ(stats.leechers, 0u);
}

TEST_F(PeerManagerTest, Configuration) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};

  manager.SetMaxConnections(100);
  EXPECT_EQ(manager.GetConfig().max_connections, 100u);

  manager.SetMaxUploadSlots(8);
  EXPECT_EQ(manager.GetConfig().max_upload_slots, 8u);
}

// ============================================================================
// Test Handler
// ============================================================================

class TestPeerManagerHandler : public IPeerManagerHandler {
 public:
  void OnPeerConnected(PeerInfo const& peer) override {
    connected_peers_.push_back(peer.endpoint);
  }

  void OnPeerDisconnected(PeerEndpoint const& endpoint,
                          std::error_code ec) override {
    disconnected_peers_.push_back(endpoint);
    disconnect_errors_.push_back(ec);
  }

  void OnPieceComplete(std::uint32_t piece_index) override {
    completed_pieces_.push_back(piece_index);
  }

  void OnPieceFailed(std::uint32_t piece_index) override {
    failed_pieces_.push_back(piece_index);
  }

  void OnDownloadComplete() override {
    download_complete_ = true;
  }

  void OnStatsUpdated(std::uint64_t downloaded, std::uint64_t uploaded,
                      double progress) override {
    last_downloaded_ = downloaded;
    last_uploaded_ = uploaded;
    last_progress_ = progress;
  }

  void Reset() {
    connected_peers_.clear();
    disconnected_peers_.clear();
    disconnect_errors_.clear();
    completed_pieces_.clear();
    failed_pieces_.clear();
    download_complete_ = false;
  }

  std::vector<PeerEndpoint> connected_peers_{};
  std::vector<PeerEndpoint> disconnected_peers_{};
  std::vector<std::error_code> disconnect_errors_{};
  std::vector<std::uint32_t> completed_pieces_{};
  std::vector<std::uint32_t> failed_pieces_{};
  bool download_complete_{false};
  std::uint64_t last_downloaded_{0};
  std::uint64_t last_uploaded_{0};
  double last_progress_{0.0};
};

TEST_F(PeerManagerTest, SetHandler) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};
  TestPeerManagerHandler handler{};

  manager.SetHandler(&handler);
  // Handler is set but no events triggered yet
}

// ============================================================================
// Choking Algorithm Tests
// ============================================================================

TEST_F(PeerManagerTest, RunChokingAlgorithmNoPeers) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};
  manager.Start();

  // Should not crash with no peers
  manager.RunChokingAlgorithm();
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST(UtilityTest, EstimateTimeRemaining) {
  // 1 MB remaining at 100 KB/s = 10 seconds
  auto eta = EstimateTimeRemaining(1024 * 1024, 100 * 1024);
  EXPECT_EQ(eta.count(), 10);
}

TEST(UtilityTest, EstimateTimeRemainingZeroRate) {
  auto eta = EstimateTimeRemaining(1024, 0.0);
  EXPECT_EQ(eta, std::chrono::seconds::max());
}

TEST(UtilityTest, EstimateTimeRemainingZeroBytes) {
  auto eta = EstimateTimeRemaining(0, 100.0);
  EXPECT_EQ(eta, std::chrono::seconds::max());
}

TEST(UtilityTest, FormatBytes) {
  EXPECT_EQ(FormatBytes(0), "0.00 B");
  EXPECT_EQ(FormatBytes(500), "500.00 B");
  EXPECT_EQ(FormatBytes(1024), "1.00 KB");
  EXPECT_EQ(FormatBytes(1536), "1.50 KB");
  EXPECT_EQ(FormatBytes(1024 * 1024), "1.00 MB");
  EXPECT_EQ(FormatBytes(1024ULL * 1024 * 1024), "1.00 GB");
  EXPECT_EQ(FormatBytes(1024ULL * 1024 * 1024 * 1024), "1.00 TB");
}

TEST(UtilityTest, FormatRate) {
  EXPECT_EQ(FormatRate(1024), "1.00 KB/s");
  EXPECT_EQ(FormatRate(1024 * 1024), "1.00 MB/s");
}

// ============================================================================
// PeerSortCriteria Tests
// ============================================================================

TEST(PeerSortCriteriaTest, EnumValues) {
  EXPECT_EQ(static_cast<int>(PeerSortCriteria::DownloadRate), 0);
  EXPECT_EQ(static_cast<int>(PeerSortCriteria::UploadRate), 1);
  EXPECT_EQ(static_cast<int>(PeerSortCriteria::PieceCount), 2);
  EXPECT_EQ(static_cast<int>(PeerSortCriteria::ConnectionTime), 3);
  EXPECT_EQ(static_cast<int>(PeerSortCriteria::Random), 4);
}

// ============================================================================
// Aggregate Stats Tests
// ============================================================================

TEST(AggregateStatsTest, DefaultValues) {
  PeerManager::AggregateStats stats{};

  EXPECT_EQ(stats.total_downloaded, 0u);
  EXPECT_EQ(stats.total_uploaded, 0u);
  EXPECT_DOUBLE_EQ(stats.download_rate, 0.0);
  EXPECT_DOUBLE_EQ(stats.upload_rate, 0.0);
  EXPECT_EQ(stats.connected_peers, 0u);
  EXPECT_EQ(stats.seeders, 0u);
  EXPECT_EQ(stats.leechers, 0u);
}

// ============================================================================
// Null Handler Tests
// ============================================================================

TEST(NullPeerManagerHandlerTest, DoesNotCrash) {
  NullPeerManagerHandler handler{};

  PeerInfo info{};
  PeerEndpoint endpoint{};

  // All these should be no-ops
  handler.OnPeerConnected(info);
  handler.OnPeerDisconnected(endpoint, {});
  handler.OnPieceComplete(0);
  handler.OnPieceFailed(0);
  handler.OnDownloadComplete();
  handler.OnStatsUpdated(0, 0, 0.0);
}

// ============================================================================
// Integration-style Tests
// ============================================================================

TEST_F(PeerManagerTest, FullLifecycle) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};
  TestPeerManagerHandler handler{};
  manager.SetHandler(&handler);

  // Start
  manager.Start();
  EXPECT_TRUE(manager.IsRunning());

  // Add peers
  manager.AddPeer({.ip = "192.168.1.1", .port = 6881});
  manager.AddPeer({.ip = "192.168.1.2", .port = 6882});
  EXPECT_EQ(manager.PendingConnectionCount(), 2u);

  // Get pending peers
  auto p1 = manager.GetNextPendingPeer();
  auto p2 = manager.GetNextPendingPeer();
  ASSERT_TRUE(p1.has_value());
  ASSERT_TRUE(p2.has_value());
  EXPECT_EQ(manager.PendingConnectionCount(), 0u);

  // Stop
  manager.Stop();
  EXPECT_FALSE(manager.IsRunning());
}

TEST_F(PeerManagerTest, MultipleStartStop) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};

  manager.Start();
  manager.Start();  // Second start should be no-op
  EXPECT_TRUE(manager.IsRunning());

  manager.Stop();
  manager.Stop();  // Second stop should be no-op
  EXPECT_FALSE(manager.IsRunning());

  manager.Start();  // Can restart
  EXPECT_TRUE(manager.IsRunning());
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(PeerManagerTest, ConcurrentAddPeers) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};
  manager.Start();

  std::vector<std::thread> threads;

  // Multiple threads adding peers
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&manager, t]() {
      for (int i = 0; i < 10; ++i) {
        manager.AddPeer({
            .ip = "192.168." + std::to_string(t) + "." + std::to_string(i),
            .port = static_cast<std::uint16_t>(6881 + i)});
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Should have added up to 40 peers
  EXPECT_LE(manager.PendingConnectionCount(), 40u);
}

TEST_F(PeerManagerTest, ConcurrentOperations) {
  PeerManager manager{torrent_, *piece_manager_, local_peer_id_};
  manager.Start();

  std::atomic<bool> running{true};
  std::vector<std::thread> threads;

  // Thread adding peers
  threads.emplace_back([&]() {
    for (int i = 0; running && i < 100; ++i) {
      manager.AddPeer({.ip = "10.0.0." + std::to_string(i % 256),
                       .port = static_cast<std::uint16_t>(6881 + i)});
    }
  });

  // Thread getting pending peers
  threads.emplace_back([&]() {
    for (int i = 0; running && i < 50; ++i) {
      std::ignore = manager.GetNextPendingPeer();
    }
  });

  // Thread checking stats
  threads.emplace_back([&]() {
    for (int i = 0; running && i < 50; ++i) {
      std::ignore = manager.GetAggregateStats();
      std::ignore = manager.ConnectionCount();
    }
  });

  // Let it run briefly
  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  running = false;

  for (auto& t : threads) {
    t.join();
  }

  // Should not crash or deadlock
  manager.Stop();
}
