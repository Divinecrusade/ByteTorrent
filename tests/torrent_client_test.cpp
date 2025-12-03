#include <gtest/gtest.h>
import torrent_client;
import torrent;
import peer_wire;
import piece_manager;
import peer_manager;
import bencode;
#include <chrono>
#include <filesystem>
#include <thread>

using namespace byte_torrent;
using namespace byte_torrent::torrent;
using namespace byte_torrent::peer_wire;
using namespace byte_torrent::piece_manager;
using namespace byte_torrent::peer_manager;
using namespace byte_torrent::bencode;

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

class TorrentClientTestBase : public ::testing::Test {
 protected:
  static constexpr std::uint32_t kTestPieceLength{32768};
  static constexpr std::size_t kTestPieceCount{10};

  void SetUp() override {
    // Create test torrent
    torrent_.announce = "http://tracker.example.com/announce";
    torrent_.info.name = "test_torrent";
    torrent_.info.piece_length = kTestPieceLength;
    torrent_.info.length = kTestPieceCount * kTestPieceLength;
    torrent_.info.pieces.resize(kTestPieceCount);
    for (std::size_t i = 0; i < kTestPieceCount; ++i) {
      std::ranges::fill(torrent_.info.pieces[i], static_cast<std::byte>(i));
    }
    // Generate info hash
    Dictionary info_dict{};
    info_dict["name"] = ToByteString(torrent_.info.name);
    info_dict["piece length"] = Integer{static_cast<std::int64_t>(kTestPieceLength)};
    info_dict["length"] = Integer{static_cast<std::int64_t>(torrent_.info.length.value())};
    ByteString pieces_data{};
    for (auto const& hash : torrent_.info.pieces) {
      for (auto b : hash) {
        pieces_data.push_back(b);
      }
    }
    info_dict["pieces"] = pieces_data;
    torrent_.info_hash = CalculateInfoHash(info_dict);
  }

  Torrent torrent_{};
};

// ============================================================================
// ClientState Tests
// ============================================================================

TEST(ClientStateTest, ToString) {
  EXPECT_EQ(ToString(ClientState::Stopped), "Stopped");
  EXPECT_EQ(ToString(ClientState::Starting), "Starting");
  EXPECT_EQ(ToString(ClientState::Downloading), "Downloading");
  EXPECT_EQ(ToString(ClientState::Seeding), "Seeding");
  EXPECT_EQ(ToString(ClientState::Paused), "Paused");
  EXPECT_EQ(ToString(ClientState::Checking), "Checking");
  EXPECT_EQ(ToString(ClientState::Error), "Error");
}

// ============================================================================
// TorrentClientConfig Tests
// ============================================================================

TEST(TorrentClientConfigTest, DefaultValues) {
  TorrentClientConfig config{};

  EXPECT_EQ(config.listen_port, 6881);
  EXPECT_EQ(config.max_connections, 50u);
  EXPECT_EQ(config.max_upload_slots, 4u);
  EXPECT_EQ(config.max_download_rate, 0u);
  EXPECT_EQ(config.max_upload_rate, 0u);
  EXPECT_EQ(config.announce_interval.count(), 1800);
  EXPECT_EQ(config.choke_interval.count(), 10);
  EXPECT_EQ(config.keep_alive_interval.count(), 120);
  EXPECT_EQ(config.block_size, kBlockSize);
  EXPECT_EQ(config.max_requests_per_peer, 5u);
  EXPECT_FALSE(config.enable_dht);
  EXPECT_FALSE(config.enable_pex);
  EXPECT_TRUE(config.enable_endgame);
  EXPECT_TRUE(config.seed_after_complete);
  EXPECT_TRUE(config.verify_on_start);
}

// ============================================================================
// ClientStats Tests
// ============================================================================

TEST(ClientStatsTest, DefaultValues) {
  ClientStats stats{};

  EXPECT_EQ(stats.downloaded_bytes, 0u);
  EXPECT_EQ(stats.uploaded_bytes, 0u);
  EXPECT_EQ(stats.total_size, 0u);
  EXPECT_DOUBLE_EQ(stats.download_rate, 0.0);
  EXPECT_DOUBLE_EQ(stats.upload_rate, 0.0);
  EXPECT_DOUBLE_EQ(stats.progress, 0.0);
  EXPECT_EQ(stats.connected_peers, 0u);
  EXPECT_EQ(stats.state, ClientState::Stopped);
  EXPECT_EQ(stats.eta, std::chrono::seconds::max());
}

// ============================================================================
// TorrentClient Basic Tests
// ============================================================================

class TorrentClientTest : public TorrentClientTestBase {};

TEST_F(TorrentClientTest, Construction) {
  TorrentClient client{torrent_};

  EXPECT_EQ(client.GetState(), ClientState::Stopped);
  EXPECT_FALSE(client.IsRunning());
  EXPECT_FALSE(client.IsComplete());
}

TEST_F(TorrentClientTest, ConstructionWithConfig) {
  TorrentClientConfig config{};
  config.listen_port = 12345;
  config.max_connections = 100;

  TorrentClient client{torrent_, config};

  EXPECT_EQ(client.GetConfig().listen_port, 12345);
  EXPECT_EQ(client.GetConfig().max_connections, 100u);
}

TEST_F(TorrentClientTest, GetTorrent) {
  TorrentClient client{torrent_};

  auto const& t = client.GetTorrent();
  EXPECT_EQ(t.announce, "http://tracker.example.com/announce");
  EXPECT_EQ(t.info.name, "test_torrent");
}

TEST_F(TorrentClientTest, GetPeerId) {
  TorrentClient client{torrent_};

  auto const& peer_id = client.GetPeerId();
  EXPECT_EQ(peer_id.size(), 20u);
  // Should start with client identifier
  EXPECT_EQ(peer_id[0], std::byte{'-'});
}

TEST_F(TorrentClientTest, InitialStats) {
  TorrentClient client{torrent_};

  auto stats = client.GetStats();
  EXPECT_EQ(stats.downloaded_bytes, 0u);
  EXPECT_EQ(stats.uploaded_bytes, 0u);
  EXPECT_EQ(stats.state, ClientState::Stopped);
}

TEST_F(TorrentClientTest, InitialProgress) {
  TorrentClient client{torrent_};

  EXPECT_DOUBLE_EQ(client.GetProgress(), 0.0);
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

TEST_F(TorrentClientTest, StartStop) {
  TorrentClientConfig config{};
  config.verify_on_start = false;  // Skip verification for test

  TorrentClient client{torrent_, config};

  client.Start();
  EXPECT_TRUE(client.IsRunning());
  EXPECT_NE(client.GetState(), ClientState::Stopped);

  client.Stop();
  EXPECT_FALSE(client.IsRunning());
  EXPECT_EQ(client.GetState(), ClientState::Stopped);
}

TEST_F(TorrentClientTest, PauseResume) {
  TorrentClientConfig config{};
  config.verify_on_start = false;

  TorrentClient client{torrent_, config};

  client.Start();
  auto initial_state = client.GetState();

  client.Pause();
  EXPECT_EQ(client.GetState(), ClientState::Paused);

  client.Resume();
  EXPECT_EQ(client.GetState(), initial_state);

  client.Stop();
}

TEST_F(TorrentClientTest, DoubleStart) {
  TorrentClientConfig config{};
  config.verify_on_start = false;

  TorrentClient client{torrent_, config};

  client.Start();
  auto state1 = client.GetState();

  client.Start();  // Should be no-op
  EXPECT_EQ(client.GetState(), state1);

  client.Stop();
}

TEST_F(TorrentClientTest, DoubleStop) {
  TorrentClientConfig config{};
  config.verify_on_start = false;

  TorrentClient client{torrent_, config};

  client.Start();
  client.Stop();
  client.Stop();  // Should be no-op

  EXPECT_EQ(client.GetState(), ClientState::Stopped);
}

TEST_F(TorrentClientTest, PauseWhileStopped) {
  TorrentClient client{torrent_};

  client.Pause();  // Should be no-op
  EXPECT_EQ(client.GetState(), ClientState::Stopped);
}

TEST_F(TorrentClientTest, ResumeWhileStopped) {
  TorrentClient client{torrent_};

  client.Resume();  // Should be no-op
  EXPECT_EQ(client.GetState(), ClientState::Stopped);
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_F(TorrentClientTest, SetMaxConnections) {
  TorrentClient client{torrent_};

  client.SetMaxConnections(100);
  EXPECT_EQ(client.GetConfig().max_connections, 100u);
}

TEST_F(TorrentClientTest, SetMaxUploadSlots) {
  TorrentClient client{torrent_};

  client.SetMaxUploadSlots(8);
  EXPECT_EQ(client.GetConfig().max_upload_slots, 8u);
}

TEST_F(TorrentClientTest, SetDownloadDir) {
  TorrentClient client{torrent_};

  client.SetDownloadDir("/tmp/downloads");
  EXPECT_EQ(client.GetConfig().download_dir, "/tmp/downloads");
}

// ============================================================================
// Peer Management Tests
// ============================================================================

TEST_F(TorrentClientTest, AddPeerWhileStopped) {
  TorrentClient client{torrent_};

  // Adding peers while stopped should fail
  bool added = client.AddPeer({.ip = "192.168.1.1", .port = 6881});
  EXPECT_FALSE(added);
}

TEST_F(TorrentClientTest, AddPeerWhileRunning) {
  TorrentClientConfig config{};
  config.verify_on_start = false;

  TorrentClient client{torrent_, config};
  client.Start();

  bool added = client.AddPeer({.ip = "192.168.1.1", .port = 6881});
  EXPECT_TRUE(added);

  client.Stop();
}

TEST_F(TorrentClientTest, AddMultiplePeers) {
  TorrentClientConfig config{};
  config.verify_on_start = false;

  TorrentClient client{torrent_, config};
  client.Start();

  std::vector<PeerEndpoint> peers{
      {.ip = "192.168.1.1", .port = 6881},
      {.ip = "192.168.1.2", .port = 6881},
      {.ip = "192.168.1.3", .port = 6881},
  };

  std::size_t added = client.AddPeers(peers);
  EXPECT_EQ(added, 3u);

  client.Stop();
}

// ============================================================================
// Piece Priority Tests
// ============================================================================

TEST_F(TorrentClientTest, SetPiecePriority) {
  TorrentClient client{torrent_};

  client.SetPiecePriority(0, PiecePriority::Critical);
  client.SetPiecePriority(5, PiecePriority::Skip);
  // Should not crash
}

TEST_F(TorrentClientTest, EnableStreamingMode) {
  TorrentClient client{torrent_};

  client.EnableStreamingMode();
  // Should prioritize first and last pieces
}

// ============================================================================
// Event Handler Tests
// ============================================================================

class TestClientHandler : public ITorrentClientHandler {
 public:
  void OnStateChanged(ClientState new_state) override {
    state_changes_.push_back(new_state);
  }

  void OnProgressUpdated(ClientStats const& stats) override {
    ++progress_updates_;
    last_stats_ = stats;
  }

  void OnPieceComplete(std::uint32_t piece_index) override {
    completed_pieces_.push_back(piece_index);
  }

  void OnDownloadComplete() override {
    download_complete_ = true;
  }

  void OnError(std::string const& message) override {
    errors_.push_back(message);
  }

  void OnPeerConnected(PeerEndpoint const& endpoint) override {
    connected_peers_.push_back(endpoint);
  }

  void OnPeerDisconnected(PeerEndpoint const& endpoint) override {
    disconnected_peers_.push_back(endpoint);
  }

  void OnTrackerResponse(std::size_t peer_count) override {
    tracker_peer_counts_.push_back(peer_count);
  }

  void OnTrackerError(std::string const& message) override {
    tracker_errors_.push_back(message);
  }

  void Reset() {
    state_changes_.clear();
    progress_updates_ = 0;
    completed_pieces_.clear();
    download_complete_ = false;
    errors_.clear();
    connected_peers_.clear();
    disconnected_peers_.clear();
    tracker_peer_counts_.clear();
    tracker_errors_.clear();
  }

  std::vector<ClientState> state_changes_{};
  std::size_t progress_updates_{0};
  ClientStats last_stats_{};
  std::vector<std::uint32_t> completed_pieces_{};
  bool download_complete_{false};
  std::vector<std::string> errors_{};
  std::vector<PeerEndpoint> connected_peers_{};
  std::vector<PeerEndpoint> disconnected_peers_{};
  std::vector<std::size_t> tracker_peer_counts_{};
  std::vector<std::string> tracker_errors_{};
};

TEST_F(TorrentClientTest, HandlerStateChanges) {
  TorrentClientConfig config{};
  config.verify_on_start = false;

  TorrentClient client{torrent_, config};
  TestClientHandler handler{};
  client.SetHandler(&handler);

  client.Start();
  client.Stop();

  // Should have received state change events
  EXPECT_FALSE(handler.state_changes_.empty());
  EXPECT_EQ(handler.state_changes_.back(), ClientState::Stopped);
}

TEST_F(TorrentClientTest, HandlerPauseResume) {
  TorrentClientConfig config{};
  config.verify_on_start = false;

  TorrentClient client{torrent_, config};
  TestClientHandler handler{};
  client.SetHandler(&handler);

  client.Start();
  handler.Reset();

  client.Pause();
  EXPECT_FALSE(handler.state_changes_.empty());
  EXPECT_EQ(handler.state_changes_.back(), ClientState::Paused);

  handler.Reset();
  client.Resume();
  EXPECT_FALSE(handler.state_changes_.empty());

  client.Stop();
}

// ============================================================================
// Null Handler Tests
// ============================================================================

TEST(NullTorrentClientHandlerTest, DoesNotCrash) {
  NullTorrentClientHandler handler{};

  handler.OnStateChanged(ClientState::Downloading);
  handler.OnProgressUpdated(ClientStats{});
  handler.OnPieceComplete(0);
  handler.OnDownloadComplete();
  handler.OnError("test error");
  handler.OnPeerConnected({.ip = "1.2.3.4", .port = 6881});
  handler.OnPeerDisconnected({.ip = "1.2.3.4", .port = 6881});
  handler.OnTrackerResponse(10);
  handler.OnTrackerError("tracker error");
}

// ============================================================================
// FormattedProgress Tests
// ============================================================================

TEST(FormattedProgressTest, FormatProgress) {
  ClientStats stats{};
  stats.downloaded_bytes = 1024 * 1024;  // 1 MB
  stats.uploaded_bytes = 512 * 1024;     // 512 KB
  stats.total_size = 10 * 1024 * 1024;   // 10 MB
  stats.remaining_bytes = 9 * 1024 * 1024;
  stats.download_rate = 100 * 1024;      // 100 KB/s
  stats.upload_rate = 50 * 1024;         // 50 KB/s
  stats.progress = 0.1;
  stats.connected_peers = 5;
  stats.seeders = 2;
  stats.leechers = 3;
  stats.eta = std::chrono::seconds{90};
  stats.state = ClientState::Downloading;

  auto fmt = FormatProgress(stats);

  EXPECT_EQ(fmt.downloaded, "1.00 MB");
  EXPECT_EQ(fmt.uploaded, "512.00 KB");
  EXPECT_EQ(fmt.total_size, "10.00 MB");
  EXPECT_EQ(fmt.download_rate, "100.00 KB/s");
  EXPECT_EQ(fmt.progress_percent, "10.0%");
  EXPECT_EQ(fmt.eta, "1m 30s");
  EXPECT_EQ(fmt.ratio, "0.50");
  EXPECT_EQ(fmt.state, "Downloading");
}

TEST(FormattedProgressTest, FormatProgressZeroDownload) {
  ClientStats stats{};
  stats.downloaded_bytes = 0;
  stats.uploaded_bytes = 1000;

  auto fmt = FormatProgress(stats);

  EXPECT_EQ(fmt.ratio, "0.00");
}

TEST(FormattedProgressTest, FormatProgressInfiniteEta) {
  ClientStats stats{};
  stats.eta = std::chrono::seconds::max();

  auto fmt = FormatProgress(stats);

  EXPECT_EQ(fmt.eta, "∞");
}

TEST(FormattedProgressTest, FormatProgressShortEta) {
  ClientStats stats{};
  stats.eta = std::chrono::seconds{45};

  auto fmt = FormatProgress(stats);

  EXPECT_EQ(fmt.eta, "45s");
}

TEST(FormattedProgressTest, FormatProgressLongEta) {
  ClientStats stats{};
  stats.eta = std::chrono::seconds{7200 + 1800};  // 2h 30m

  auto fmt = FormatProgress(stats);

  EXPECT_EQ(fmt.eta, "2h 30m");
}

// ============================================================================
// Update Loop Tests
// ============================================================================

TEST_F(TorrentClientTest, UpdateWhileStopped) {
  TorrentClient client{torrent_};

  // Should be safe to call update while stopped
  client.Update();
}

TEST_F(TorrentClientTest, UpdateWhileRunning) {
  TorrentClientConfig config{};
  config.verify_on_start = false;

  TorrentClient client{torrent_, config};
  client.Start();

  // Should process pending tasks
  for (int i = 0; i < 10; ++i) {
    client.Update();
  }

  client.Stop();
}

// ============================================================================
// Force Actions Tests
// ============================================================================

TEST_F(TorrentClientTest, ForceReannounce) {
  TorrentClientConfig config{};
  config.verify_on_start = false;

  TorrentClient client{torrent_, config};
  client.Start();

  // Should not crash (actual announce will fail without network)
  client.ForceReannounce();

  client.Stop();
}

TEST_F(TorrentClientTest, ForceRecheck) {
  TorrentClientConfig config{};
  config.verify_on_start = false;

  TorrentClient client{torrent_, config};
  TestClientHandler handler{};
  client.SetHandler(&handler);
  client.Start();

  handler.Reset();
  client.ForceRecheck();

  // Should have entered Checking state temporarily
  bool entered_checking = std::ranges::any_of(
      handler.state_changes_,
      [](ClientState s) { return s == ClientState::Checking; });
  EXPECT_TRUE(entered_checking);

  client.Stop();
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(TorrentClientTest, ConcurrentAccess) {
  TorrentClientConfig config{};
  config.verify_on_start = false;

  TorrentClient client{torrent_, config};
  client.Start();

  std::atomic<bool> running{true};
  std::vector<std::thread> threads;

  // Thread reading stats
  threads.emplace_back([&]() {
    while (running) {
      std::ignore = client.GetStats();
      std::ignore = client.GetProgress();
      std::ignore = client.GetState();
    }
  });

  // Thread adding peers
  threads.emplace_back([&]() {
    for (int i = 0; running && i < 50; ++i) {
      client.AddPeer({.ip = "10.0.0." + std::to_string(i % 256),
                      .port = static_cast<std::uint16_t>(6881 + i)});
    }
  });

  // Thread calling update
  threads.emplace_back([&]() {
    for (int i = 0; running && i < 20; ++i) {
      client.Update();
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds{50});
  running = false;

  for (auto& t : threads) {
    t.join();
  }

  client.Stop();
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(TorrentClientTest, FullLifecycle) {
  TorrentClientConfig config{};
  config.verify_on_start = false;
  config.max_connections = 10;

  TorrentClient client{torrent_, config};
  TestClientHandler handler{};
  client.SetHandler(&handler);

  // Start
  client.Start();
  EXPECT_TRUE(client.IsRunning());

  // Add some peers
  std::vector<PeerEndpoint> peers;
  for (int i = 0; i < 5; ++i) {
    peers.push_back({.ip = "192.168.1." + std::to_string(i + 1),
                     .port = static_cast<std::uint16_t>(6881 + i)});
  }
  client.AddPeers(peers);

  // Run a few update cycles
  for (int i = 0; i < 5; ++i) {
    client.Update();
  }

  // Pause
  client.Pause();
  EXPECT_EQ(client.GetState(), ClientState::Paused);

  // Resume
  client.Resume();
  EXPECT_NE(client.GetState(), ClientState::Paused);

  // Stop
  client.Stop();
  EXPECT_FALSE(client.IsRunning());
  EXPECT_EQ(client.GetState(), ClientState::Stopped);
}

// ============================================================================
// Socket Factory Tests
// ============================================================================

class MockSocketFactory : public ISocketFactory {
 public:
  std::unique_ptr<ISocket> CreateSocket() override {
    ++sockets_created_;
    return nullptr;  // Would return mock socket
  }

  void RunEventLoop() override { running_ = true; }
  void StopEventLoop() override { running_ = false; }
  bool IsRunning() const override { return running_; }

  std::size_t sockets_created_{0};
  bool running_{false};
};

TEST_F(TorrentClientTest, SetSocketFactory) {
  TorrentClient client{torrent_};
  MockSocketFactory factory{};

  client.SetSocketFactory(&factory);
  // Factory is set for connection creation
}
