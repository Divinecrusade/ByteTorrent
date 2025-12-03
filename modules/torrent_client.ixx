export module torrent_client;

import std;
import bencode;
import torrent;
import peer_wire;
import tracker;
import piece_manager;
import peer_manager;

namespace byte_torrent {

// ============================================================================
// Client State
// ============================================================================

export enum class ClientState {
  Stopped,
  Starting,
  Downloading,
  Seeding,
  Paused,
  Checking,  // Verifying existing data
  Error
};

export [[nodiscard]] constexpr std::string_view ToString(ClientState state) noexcept {
  switch (state) {
    case ClientState::Stopped:     return "Stopped";
    case ClientState::Starting:    return "Starting";
    case ClientState::Downloading: return "Downloading";
    case ClientState::Seeding:     return "Seeding";
    case ClientState::Paused:      return "Paused";
    case ClientState::Checking:    return "Checking";
    case ClientState::Error:       return "Error";
    default:                       return "Unknown";
  }
}

// ============================================================================
// Client Configuration
// ============================================================================

export struct TorrentClientConfig {
  // Network settings
  std::uint16_t listen_port{6881};
  std::size_t max_connections{50};
  std::size_t max_upload_slots{4};

  // Transfer settings
  std::uint64_t max_download_rate{0};  // 0 = unlimited (bytes/sec)
  std::uint64_t max_upload_rate{0};    // 0 = unlimited

  // Timing settings
  std::chrono::seconds announce_interval{1800};      // 30 min default
  std::chrono::seconds min_announce_interval{60};
  std::chrono::seconds choke_interval{10};
  std::chrono::seconds keep_alive_interval{120};
  std::chrono::seconds request_timeout{60};
  std::chrono::seconds connect_timeout{30};

  // Piece settings
  std::uint32_t block_size{peer_wire::kBlockSize};
  std::size_t max_requests_per_peer{5};
  piece_manager::SelectionStrategy selection_strategy{
      piece_manager::SelectionStrategy::RarestFirst};

  // Behavior settings
  bool enable_dht{false};           // Future: DHT support
  bool enable_pex{false};           // Future: Peer Exchange
  bool enable_endgame{true};
  bool seed_after_complete{true};
  bool verify_on_start{true};

  // Directories
  std::filesystem::path download_dir{"."};
};

// ============================================================================
// Client Statistics
// ============================================================================

export struct ClientStats {
  // Transfer stats
  std::uint64_t downloaded_bytes{0};
  std::uint64_t uploaded_bytes{0};
  std::uint64_t total_size{0};
  std::uint64_t remaining_bytes{0};

  // Rate stats
  double download_rate{0.0};  // bytes/sec
  double upload_rate{0.0};

  // Progress
  double progress{0.0};  // 0.0 - 1.0
  std::size_t verified_pieces{0};
  std::size_t total_pieces{0};

  // Peers
  std::size_t connected_peers{0};
  std::size_t seeders{0};
  std::size_t leechers{0};
  std::size_t pending_peers{0};

  // Tracker
  std::optional<std::chrono::system_clock::time_point> last_announce{};
  std::optional<std::chrono::system_clock::time_point> next_announce{};
  std::optional<std::string> tracker_error{};

  // Timing
  std::chrono::steady_clock::time_point started_at{};
  std::chrono::seconds eta{std::chrono::seconds::max()};

  // State
  ClientState state{ClientState::Stopped};
};

// ============================================================================
// Client Events Interface
// ============================================================================

export class ITorrentClientHandler {
 public:
  virtual ~ITorrentClientHandler() = default;

  virtual void OnStateChanged(ClientState new_state) = 0;
  virtual void OnProgressUpdated(ClientStats const& stats) = 0;
  virtual void OnPieceComplete(std::uint32_t piece_index) = 0;
  virtual void OnDownloadComplete() = 0;
  virtual void OnError(std::string const& message) = 0;
  virtual void OnPeerConnected(peer_wire::PeerEndpoint const& endpoint) = 0;
  virtual void OnPeerDisconnected(peer_wire::PeerEndpoint const& endpoint) = 0;
  virtual void OnTrackerResponse(std::size_t peer_count) = 0;
  virtual void OnTrackerError(std::string const& message) = 0;
};

// ============================================================================
// Null Handler
// ============================================================================

export class NullTorrentClientHandler : public ITorrentClientHandler {
 public:
  void OnStateChanged(ClientState) override {}
  void OnProgressUpdated(ClientStats const&) override {}
  void OnPieceComplete(std::uint32_t) override {}
  void OnDownloadComplete() override {}
  void OnError(std::string const&) override {}
  void OnPeerConnected(peer_wire::PeerEndpoint const&) override {}
  void OnPeerDisconnected(peer_wire::PeerEndpoint const&) override {}
  void OnTrackerResponse(std::size_t) override {}
  void OnTrackerError(std::string const&) override {}
};

// ============================================================================
// Periodic Task Scheduler
// ============================================================================

class TaskScheduler {
 public:
  using Task = std::function<void()>;

  struct ScheduledTask {
    std::string name{};
    Task task{};
    std::chrono::steady_clock::duration interval{};
    std::chrono::steady_clock::time_point next_run{};
    bool enabled{true};
  };

  void AddTask(std::string name, Task task,
               std::chrono::steady_clock::duration interval) {
    std::lock_guard lock{mutex_};
    tasks_.push_back({
        .name = std::move(name),
        .task = std::move(task),
        .interval = interval,
        .next_run = std::chrono::steady_clock::now() + interval,
        .enabled = true});
  }

  void EnableTask(std::string_view name, bool enabled) {
    std::lock_guard lock{mutex_};
    for (auto& task : tasks_) {
      if (task.name == name) {
        task.enabled = enabled;
        break;
      }
    }
  }

  void RunDueTasks() {
    std::lock_guard lock{mutex_};
    auto const now = std::chrono::steady_clock::now();

    for (auto& task : tasks_) {
      if (task.enabled && now >= task.next_run) {
        try {
          task.task();
        } catch (...) {
          // Log error but continue
        }
        task.next_run = now + task.interval;
      }
    }
  }

  void Clear() {
    std::lock_guard lock{mutex_};
    tasks_.clear();
  }

  [[nodiscard]] std::optional<std::chrono::steady_clock::duration>
  TimeUntilNextTask() const {
    std::lock_guard lock{mutex_};
    if (tasks_.empty()) return std::nullopt;

    auto const now = std::chrono::steady_clock::now();
    auto min_wait = std::chrono::steady_clock::duration::max();

    for (auto const& task : tasks_) {
      if (task.enabled && task.next_run > now) {
        auto wait = task.next_run - now;
        if (wait < min_wait) {
          min_wait = wait;
        }
      }
    }

    if (min_wait == std::chrono::steady_clock::duration::max()) {
      return std::chrono::milliseconds{100};  // Default poll interval
    }

    return min_wait;
  }

 private:
  mutable std::mutex mutex_{};
  std::vector<ScheduledTask> tasks_{};
};

// ============================================================================
// Socket Factory Interface (for dependency injection)
// ============================================================================

export class ISocketFactory {
 public:
  virtual ~ISocketFactory() = default;

  virtual std::unique_ptr<peer_wire::ISocket> CreateSocket() = 0;
  virtual void RunEventLoop() = 0;
  virtual void StopEventLoop() = 0;
  virtual bool IsRunning() const = 0;
};

// ============================================================================
// Torrent Client
// ============================================================================

export class TorrentClient : public peer_manager::IPeerManagerHandler {
 public:
  TorrentClient(torrent::Torrent torrent,
                TorrentClientConfig config = {})
      : torrent_{std::move(torrent)},
        config_{std::move(config)},
        local_peer_id_{peer_wire::GeneratePeerId("-BT0001-")},
        piece_manager_{torrent_.info, MakePieceManagerConfig()},
        peer_manager_{torrent_, piece_manager_, local_peer_id_,
                      MakePeerManagerConfig()},
        tracker_client_{torrent_.announce} {
    peer_manager_.SetHandler(this);
    SetupPeriodicTasks();
  }

  ~TorrentClient() { Stop(); }

  // Non-copyable
  TorrentClient(TorrentClient const&) = delete;
  TorrentClient& operator=(TorrentClient const&) = delete;

  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------

  void Start() {
    std::lock_guard lock{mutex_};

    if (state_ != ClientState::Stopped && state_ != ClientState::Paused) {
      return;
    }

    SetState(ClientState::Starting);
    stats_.started_at = std::chrono::steady_clock::now();
    stats_.total_size = torrent_.TotalLength();
    stats_.total_pieces = torrent_.PieceCount();

    // Verify existing data if configured
    if (config_.verify_on_start) {
      SetState(ClientState::Checking);
      VerifyExistingData();
    }

    // Start peer manager
    peer_manager_.Start();

    // Initial tracker announce
    AnnounceToTracker(tracker::TrackerEvent::Started);

    // Determine initial state
    if (piece_manager_.IsComplete()) {
      SetState(ClientState::Seeding);
    } else {
      SetState(ClientState::Downloading);
    }

    running_ = true;
  }

  void Stop() {
    std::lock_guard lock{mutex_};

    if (state_ == ClientState::Stopped) return;

    running_ = false;

    // Announce stopped to tracker
    AnnounceToTracker(tracker::TrackerEvent::Stopped);

    // Stop peer manager
    peer_manager_.Stop();

    // Clear tasks
    scheduler_.Clear();

    SetState(ClientState::Stopped);
  }

  void Pause() {
    std::lock_guard lock{mutex_};

    if (state_ != ClientState::Downloading && state_ != ClientState::Seeding) {
      return;
    }

    // Keep connections but stop requesting
    SetState(ClientState::Paused);
  }

  void Resume() {
    std::lock_guard lock{mutex_};

    if (state_ != ClientState::Paused) return;

    if (piece_manager_.IsComplete()) {
      SetState(ClientState::Seeding);
    } else {
      SetState(ClientState::Downloading);
    }
  }

  // -------------------------------------------------------------------------
  // Main Loop (call periodically or run in thread)
  // -------------------------------------------------------------------------

  void Update() {
    if (!running_) return;

    // Run scheduled tasks
    scheduler_.RunDueTasks();

    // Process pending peer connections
    ProcessPendingConnections();

    // Update statistics
    UpdateStats();
  }

  // Run blocking event loop
  void Run() {
    while (running_) {
      Update();

      // Sleep until next task
      auto wait_time = scheduler_.TimeUntilNextTask();
      if (wait_time) {
        std::this_thread::sleep_for(*wait_time);
      }
    }
  }

  // -------------------------------------------------------------------------
  // IPeerManagerHandler Implementation
  // -------------------------------------------------------------------------

  void OnPeerConnected(peer_manager::PeerInfo const& peer) override {
    if (handler_) {
      handler_->OnPeerConnected(peer.endpoint);
    }
  }

  void OnPeerDisconnected(peer_wire::PeerEndpoint const& endpoint,
                          std::error_code) override {
    if (handler_) {
      handler_->OnPeerDisconnected(endpoint);
    }
  }

  void OnPieceComplete(std::uint32_t piece_index) override {
    if (handler_) {
      handler_->OnPieceComplete(piece_index);
    }

    // Write piece to disk
    WritePieceToDisk(piece_index);
  }

  void OnPieceFailed(std::uint32_t) override {
    // PieceManager handles reset, we just track stats
    ++stats_wasted_pieces_;
  }

  void OnDownloadComplete() override {
    std::lock_guard lock{mutex_};

    // Announce completion to tracker
    AnnounceToTracker(tracker::TrackerEvent::Completed);

    if (config_.seed_after_complete) {
      SetState(ClientState::Seeding);
    } else {
      Stop();
    }

    if (handler_) {
      handler_->OnDownloadComplete();
    }
  }

  void OnStatsUpdated(std::uint64_t downloaded, std::uint64_t uploaded,
                      double progress) override {
    stats_.downloaded_bytes = downloaded;
    stats_.uploaded_bytes = uploaded;
    stats_.progress = progress;
  }

  // -------------------------------------------------------------------------
  // Queries
  // -------------------------------------------------------------------------

  [[nodiscard]] ClientState GetState() const {
    std::lock_guard lock{mutex_};
    return state_;
  }

  [[nodiscard]] ClientStats GetStats() const {
    std::lock_guard lock{mutex_};
    return stats_;
  }

  [[nodiscard]] torrent::Torrent const& GetTorrent() const noexcept {
    return torrent_;
  }

  [[nodiscard]] TorrentClientConfig const& GetConfig() const noexcept {
    return config_;
  }

  [[nodiscard]] peer_wire::PeerId const& GetPeerId() const noexcept {
    return local_peer_id_;
  }

  [[nodiscard]] bool IsRunning() const noexcept { return running_; }

  [[nodiscard]] bool IsComplete() const {
    return piece_manager_.IsComplete();
  }

  [[nodiscard]] double GetProgress() const {
    return piece_manager_.GetProgress();
  }

  // -------------------------------------------------------------------------
  // Configuration
  // -------------------------------------------------------------------------

  void SetHandler(ITorrentClientHandler* handler) { handler_ = handler; }

  void SetSocketFactory(ISocketFactory* factory) { socket_factory_ = factory; }

  void SetMaxConnections(std::size_t max) {
    config_.max_connections = max;
    peer_manager_.SetMaxConnections(max);
  }

  void SetMaxUploadSlots(std::size_t max) {
    config_.max_upload_slots = max;
    peer_manager_.SetMaxUploadSlots(max);
  }

  void SetDownloadDir(std::filesystem::path dir) {
    config_.download_dir = std::move(dir);
  }

  // -------------------------------------------------------------------------
  // Manual Peer Management
  // -------------------------------------------------------------------------

  bool AddPeer(peer_wire::PeerEndpoint const& endpoint) {
    return peer_manager_.AddPeer(endpoint);
  }

  std::size_t AddPeers(std::vector<peer_wire::PeerEndpoint> const& peers) {
    return peer_manager_.AddPeers(peers);
  }

  void DisconnectPeer(peer_wire::PeerEndpoint const& endpoint) {
    peer_manager_.DisconnectPeer(endpoint);
  }

  // -------------------------------------------------------------------------
  // Piece Priority
  // -------------------------------------------------------------------------

  void SetPiecePriority(std::uint32_t piece_index,
                        piece_manager::PiecePriority priority) {
    piece_manager_.SetPiecePriority(piece_index, priority);
  }

  // Prioritize first and last pieces (for preview)
  void EnableStreamingMode() {
    std::size_t const piece_count = torrent_.PieceCount();
    if (piece_count > 0) {
      piece_manager_.SetPiecePriority(0, piece_manager::PiecePriority::Critical);
    }
    if (piece_count > 1) {
      piece_manager_.SetPiecePriority(
          static_cast<std::uint32_t>(piece_count - 1),
          piece_manager::PiecePriority::Critical);
    }
  }

  // -------------------------------------------------------------------------
  // Force Actions
  // -------------------------------------------------------------------------

  void ForceReannounce() {
    AnnounceToTracker(tracker::TrackerEvent::None);
  }

  void ForceRecheck() {
    std::lock_guard lock{mutex_};
    auto prev_state = state_;
    SetState(ClientState::Checking);
    VerifyExistingData();
    SetState(prev_state);
  }

 private:
  // -------------------------------------------------------------------------
  // Internal Helpers
  // -------------------------------------------------------------------------

  void SetState(ClientState new_state) {
    if (state_ == new_state) return;
    state_ = new_state;
    stats_.state = new_state;

    if (handler_) {
      handler_->OnStateChanged(new_state);
    }
  }

  [[nodiscard]] piece_manager::PieceManagerConfig MakePieceManagerConfig() const {
    return {
        .block_size = config_.block_size,
        .max_pending_requests_per_peer = config_.max_requests_per_peer,
        .endgame_threshold = 10,
        .selection_strategy = config_.selection_strategy,
        .enable_endgame = config_.enable_endgame};
  }

  [[nodiscard]] peer_manager::PeerManagerConfig MakePeerManagerConfig() const {
    return {
        .max_connections = config_.max_connections,
        .max_upload_slots = config_.max_upload_slots,
        .optimistic_unchoke_slots = 1,
        .choke_interval = config_.choke_interval,
        .optimistic_unchoke_interval = std::chrono::seconds{30},
        .request_timeout = config_.request_timeout,
        .connect_timeout = config_.connect_timeout,
        .max_requests_per_peer = config_.max_requests_per_peer,
        .min_peers_for_endgame = 5,
        .enable_endgame = config_.enable_endgame};
  }

  void SetupPeriodicTasks() {
    // Choking algorithm
    scheduler_.AddTask("choke", [this]() { peer_manager_.RunChokingAlgorithm(); },
                       config_.choke_interval);

    // Keep-alive
    scheduler_.AddTask("keepalive", [this]() { peer_manager_.SendKeepAlives(); },
                       config_.keep_alive_interval);

    // Stats update
    scheduler_.AddTask("stats", [this]() { UpdateStats(); },
                       std::chrono::seconds{1});

    // Tracker announce (will be rescheduled based on response)
    scheduler_.AddTask("announce", [this]() {
      AnnounceToTracker(tracker::TrackerEvent::None);
    }, config_.announce_interval);
  }

  void AnnounceToTracker(tracker::TrackerEvent event) {
    tracker::AnnounceRequest request{};
    std::copy(torrent_.info_hash.begin(), torrent_.info_hash.end(),
              request.info_hash.begin());
    std::copy(local_peer_id_.begin(), local_peer_id_.end(),
              request.peer_id.begin());
    request.port = config_.listen_port;
    request.uploaded = stats_.uploaded_bytes;
    request.downloaded = stats_.downloaded_bytes;
    request.left = stats_.remaining_bytes;
    request.event = event;
    request.compact = true;

    // Note: In real implementation, this would be async
    auto result = tracker_client_.Announce(request);

    if (result.IsOk() && result.value) {
      auto const& response = *result.value;

      // Add peers from tracker
      std::size_t added = peer_manager_.AddPeers(response.peers);

      // Update announce timing
      stats_.last_announce = std::chrono::system_clock::now();
      stats_.next_announce = *stats_.last_announce +
                             std::chrono::seconds{response.interval};
      stats_.tracker_error = std::nullopt;

      // Update seeder/leecher counts from tracker
      if (response.complete) {
        stats_.seeders = response.complete;
      }
      if (response.incomplete) {
        stats_.leechers = response.incomplete;
      }

      if (handler_) {
        handler_->OnTrackerResponse(added);
      }
    } else {
      stats_.tracker_error = result.error.message;
      if (handler_) {
        handler_->OnTrackerError(result.error.message);
      }
    }
  }

  void ProcessPendingConnections() {
    if (!socket_factory_) return;

    // Try to connect to pending peers
    while (peer_manager_.ConnectionCount() < config_.max_connections) {
      auto endpoint = peer_manager_.GetNextPendingPeer();
      if (!endpoint) break;

      try {
        auto socket = socket_factory_->CreateSocket();
        auto connection = peer_wire::CreateConnection(
            std::move(socket),
            torrent_.info_hash,
            local_peer_id_,
            torrent_.PieceCount(),
            peer_wire::ConnectionConfig{
                .connect_timeout = config_.connect_timeout,
                .max_pending_requests = config_.max_requests_per_peer});

        connection->Connect(*endpoint);
        peer_manager_.OnPeerConnected(std::move(connection));
      } catch (...) {
        // Connection failed, continue with next peer
      }
    }
  }

  void UpdateStats() {
    auto pm_stats = piece_manager_.GetStats();
    auto peer_stats = peer_manager_.GetAggregateStats();

    stats_.downloaded_bytes = pm_stats.downloaded_bytes;
    stats_.remaining_bytes = pm_stats.total_bytes - pm_stats.downloaded_bytes;
    stats_.verified_pieces = pm_stats.verified_pieces;
    stats_.progress = pm_stats.Progress();

    stats_.connected_peers = peer_stats.connected_peers;
    stats_.download_rate = peer_stats.download_rate;
    stats_.upload_rate = peer_stats.upload_rate;
    stats_.uploaded_bytes = peer_stats.total_uploaded;

    // Calculate ETA
    if (stats_.download_rate > 0 && stats_.remaining_bytes > 0) {
      stats_.eta = std::chrono::seconds{
          static_cast<std::int64_t>(stats_.remaining_bytes / stats_.download_rate)};
    } else {
      stats_.eta = std::chrono::seconds::max();
    }

    stats_.pending_peers = peer_manager_.PendingConnectionCount();

    if (handler_) {
      handler_->OnProgressUpdated(stats_);
    }
  }

  void VerifyExistingData() {
    // TODO: Read pieces from disk and verify hashes
    // For now, assume no existing data
  }

  void WritePieceToDisk(std::uint32_t piece_index) {
    auto piece_data = piece_manager_.GetPieceData(piece_index);
    if (!piece_data) return;

    // TODO: Implement actual disk I/O
    // For single-file torrents:
    //   - Calculate file offset from piece_index * piece_length
    //   - Write data at that offset
    // For multi-file torrents:
    //   - Calculate which files this piece spans
    //   - Write appropriate portions to each file
    std::ignore = piece_index;
  }

  // -------------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------------

  torrent::Torrent torrent_{};
  TorrentClientConfig config_{};
  peer_wire::PeerId local_peer_id_{};

  piece_manager::PieceManager piece_manager_;
  peer_manager::PeerManager peer_manager_;
  tracker::TrackerClient tracker_client_;
  TaskScheduler scheduler_{};

  mutable std::mutex mutex_{};
  std::atomic<bool> running_{false};
  ClientState state_{ClientState::Stopped};
  ClientStats stats_{};
  std::size_t stats_wasted_pieces_{0};

  ITorrentClientHandler* handler_{nullptr};
  ISocketFactory* socket_factory_{nullptr};
};

// ============================================================================
// Factory Functions
// ============================================================================

// Create client from .torrent file path
export [[nodiscard]] std::unique_ptr<TorrentClient> CreateClientFromFile(
    std::filesystem::path const& torrent_path,
    TorrentClientConfig config = {}) {
  auto torrent = torrent::ParseFromFile(torrent_path);
  return std::make_unique<TorrentClient>(std::move(torrent), std::move(config));
}

// Create client from torrent data in memory
export [[nodiscard]] std::unique_ptr<TorrentClient> CreateClientFromData(
    std::span<char const> data,
    TorrentClientConfig config = {}) {
  auto torrent = torrent::ParseFromSpan(data);
  return std::make_unique<TorrentClient>(std::move(torrent), std::move(config));
}

// ============================================================================
// Utility: Progress Formatter
// ============================================================================

export struct FormattedProgress {
  std::string downloaded{};
  std::string uploaded{};
  std::string total_size{};
  std::string remaining{};
  std::string download_rate{};
  std::string upload_rate{};
  std::string eta{};
  std::string progress_percent{};
  std::string ratio{};
  std::string peers{};
  std::string state{};
};

export [[nodiscard]] FormattedProgress FormatProgress(ClientStats const& stats) {
  FormattedProgress fmt{};

  fmt.downloaded = peer_manager::FormatBytes(stats.downloaded_bytes);
  fmt.uploaded = peer_manager::FormatBytes(stats.uploaded_bytes);
  fmt.total_size = peer_manager::FormatBytes(stats.total_size);
  fmt.remaining = peer_manager::FormatBytes(stats.remaining_bytes);
  fmt.download_rate = peer_manager::FormatRate(stats.download_rate);
  fmt.upload_rate = peer_manager::FormatRate(stats.upload_rate);
  fmt.progress_percent = std::format("{:.1f}%", stats.progress * 100.0);

  // Format ETA
  if (stats.eta == std::chrono::seconds::max()) {
    fmt.eta = "n/a";
  } else {
    auto const secs = stats.eta.count();
    if (secs < 60) {
      fmt.eta = std::format("{}s", secs);
    } else if (secs < 3600) {
      fmt.eta = std::format("{}m {}s", secs / 60, secs % 60);
    } else {
      fmt.eta = std::format("{}h {}m", secs / 3600, (secs % 3600) / 60);
    }
  }

  // Format ratio
  if (stats.downloaded_bytes > 0) {
    double ratio = static_cast<double>(stats.uploaded_bytes) /
                   static_cast<double>(stats.downloaded_bytes);
    fmt.ratio = std::format("{:.2f}", ratio);
  } else {
    fmt.ratio = "0.00";
  }

  // Format peers
  fmt.peers = std::format("{} ({} S / {} L)",
                          stats.connected_peers,
                          stats.seeders,
                          stats.leechers);

  fmt.state = std::string{ToString(stats.state)};

  return fmt;
}

// ============================================================================
// Simple Console Progress Display
// ============================================================================

export void PrintProgress(ClientStats const& stats) {
  auto const fmt = FormatProgress(stats);

  std::print("[{}] {} / {} ({}) | Download speed {} | Upload speed {} | Peers: {} | ETA: {}\n",
             fmt.state,
             fmt.downloaded,
             fmt.total_size,
             fmt.progress_percent,
             fmt.download_rate,
             fmt.upload_rate,
             fmt.peers,
             fmt.eta);
}

}  // namespace byte_torrent
