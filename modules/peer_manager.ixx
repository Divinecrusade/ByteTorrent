export module peer_manager;

import std;
import peer_wire;
import piece_manager;
import torrent;

namespace byte_torrent::peer_manager {

// ============================================================================
// Peer Manager Configuration
// ============================================================================

export struct PeerManagerConfig {
  std::size_t max_connections{50};
  std::size_t max_upload_slots{4};       // Unchoked peers for upload
  std::size_t optimistic_unchoke_slots{1};
  std::chrono::seconds choke_interval{10};
  std::chrono::seconds optimistic_unchoke_interval{30};
  std::chrono::seconds request_timeout{60};
  std::chrono::seconds connect_timeout{30};
  std::size_t max_requests_per_peer{5};
  std::size_t min_peers_for_endgame{5};
  bool enable_endgame{true};
};

// ============================================================================
// Peer Info - Extended peer data for manager
// ============================================================================

export struct PeerInfo {
  peer_wire::PeerEndpoint endpoint{};
  std::optional<peer_wire::PeerId> peer_id{};
  peer_wire::PeerState state{};
  peer_wire::PeerStats stats{};
  peer_wire::Bitfield bitfield{};

  // Manager-specific state
  bool is_seed{false};
  bool is_snubbed{false};  // No data received in last 60s
  std::chrono::steady_clock::time_point last_piece_time{};
  std::chrono::steady_clock::time_point connected_at{};

  // Calculated metrics
  [[nodiscard]] double DownloadRate() const noexcept {
    return stats.DownloadRate();
  }

  [[nodiscard]] double UploadRate() const noexcept {
    return stats.UploadRate();
  }

  [[nodiscard]] bool IsInteresting(
      peer_wire::Bitfield const& local_bitfield) const noexcept {
    // Peer is interesting if they have pieces we don't
    for (std::size_t i = 0; i < bitfield.BitCount(); ++i) {
      if (bitfield.HasPiece(i) && !local_bitfield.HasPiece(i)) {
        return true;
      }
    }
    return false;
  }
};

// ============================================================================
// Peer Selection Criteria
// ============================================================================

export enum class PeerSortCriteria {
  DownloadRate,    // Sort by download speed (for unchoking)
  UploadRate,      // Sort by upload speed (for reciprocation)
  PieceCount,      // Sort by number of pieces (prefer seeders)
  ConnectionTime,  // Sort by how long connected
  Random           // Random selection
};

// ============================================================================
// Peer Manager Events
// ============================================================================

export class IPeerManagerHandler {
 public:
  virtual ~IPeerManagerHandler() = default;

  virtual void OnPeerConnected(PeerInfo const& peer) = 0;
  virtual void OnPeerDisconnected(peer_wire::PeerEndpoint const& endpoint,
                                   std::error_code ec) = 0;
  virtual void OnPieceComplete(std::uint32_t piece_index) = 0;
  virtual void OnPieceFailed(std::uint32_t piece_index) = 0;
  virtual void OnDownloadComplete() = 0;
  virtual void OnStatsUpdated(std::uint64_t downloaded, std::uint64_t uploaded,
                              double progress) = 0;
};

// ============================================================================
// Null Handler
// ============================================================================

export class NullPeerManagerHandler : public IPeerManagerHandler {
 public:
  void OnPeerConnected(PeerInfo const&) override {}
  void OnPeerDisconnected(peer_wire::PeerEndpoint const&, std::error_code) override {}
  void OnPieceComplete(std::uint32_t) override {}
  void OnPieceFailed(std::uint32_t) override {}
  void OnDownloadComplete() override {}
  void OnStatsUpdated(std::uint64_t, std::uint64_t, double) override {}
};

// ============================================================================
// Managed Peer - Connection + Handler
// ============================================================================

class ManagedPeer : public peer_wire::IConnectionHandler {
 public:
  ManagedPeer(std::shared_ptr<peer_wire::PeerConnection> connection,
              piece_manager::PieceManager& piece_manager,
              std::function<void(ManagedPeer*)> on_disconnect)
      : connection_{std::move(connection)},
        piece_manager_{piece_manager},
        on_disconnect_{std::move(on_disconnect)} {
    connection_->SetHandler(this);
    info_.endpoint = connection_->GetEndpoint();
    info_.bitfield = peer_wire::Bitfield{piece_manager_.PieceCount()};
  }

  // IConnectionHandler implementation
  void OnConnected() override {
    info_.connected_at = std::chrono::steady_clock::now();
    std::println("[PROTO] {} - Handshake complete, sending bitfield",
                 info_.endpoint.ToString());
    // Send our bitfield after handshake
    auto local_bf = piece_manager_.GetLocalBitfield();
    std::println("[PROTO] {} - Our bitfield: {} pieces set",
                 info_.endpoint.ToString(), local_bf.CountPieces());
    connection_->SendBitfield(local_bf);
  }

  void OnHandshakeComplete(peer_wire::HandshakeData const& hs) override {
    info_.peer_id = hs.peer_id;
  }

  void OnDisconnected(std::error_code ec) override {
    // Cancel all pending requests
    piece_manager_.CancelAllRequests(connection_->GetPendingRequests());
    piece_manager_.OnPeerDisconnected(info_.bitfield);
    if (on_disconnect_) {
      on_disconnect_(this);
    }
    disconnect_error_ = ec;
  }

  void OnError(std::error_code ec) override {
    std::ignore = ec;
  }

  void OnMessage(peer_wire::Message const&) override {
    // Stats updated in specific handlers
  }

  void OnChoke() override {
    std::println("[PROTO] {} - Received CHOKE", info_.endpoint.ToString());
    info_.state.peer_choking = true;
    // Cancel pending requests
    piece_manager_.CancelAllRequests(connection_->GetPendingRequests());
  }

  void OnUnchoke() override {
    std::println("[PROTO] {} - Received UNCHOKE!", info_.endpoint.ToString());
    info_.state.peer_choking = false;
    RequestMoreBlocks();
  }

  void OnInterested() override {
    info_.state.peer_interested = true;
  }

  void OnNotInterested() override {
    info_.state.peer_interested = false;
  }

  void OnHave(std::uint32_t piece_index) override {
    std::println("[PROTO] {} - HAVE piece {}", info_.endpoint.ToString(),
                 piece_index);
    info_.bitfield.SetPiece(piece_index);
    piece_manager_.OnPeerHave(piece_index);
    UpdateInterestState();
  }

  void OnBitfield(peer_wire::Bitfield const& bf) override {
    std::println("[PROTO] {} - Received bitfield: {}/{} pieces",
                 info_.endpoint.ToString(), bf.CountPieces(), bf.BitCount());
    info_.bitfield = bf;
    piece_manager_.OnPeerBitfield(bf);
    info_.is_seed = (bf.CountPieces() == bf.BitCount());
    std::println("[PROTO] {} - Peer is_seed={}", info_.endpoint.ToString(),
                 info_.is_seed);
    UpdateInterestState();
  }

  void OnRequest(peer_wire::BlockInfo const& block) override {
    // Handle upload request
    if (info_.state.am_choking) {
      return;  // We're choking them, ignore
    }

    auto piece_data = piece_manager_.GetPieceData(block.piece_index);
    if (!piece_data || block.offset + block.length > piece_data->size()) {
      return;  // Invalid request
    }

    auto data_span = piece_data->subspan(block.offset, block.length);
    connection_->SendPiece(block.piece_index, block.offset, data_span);
  }

  void OnPiece(std::uint32_t piece_index, std::uint32_t offset,
               std::span<std::byte const> data) override {
    std::println("[PROTO] {} - PIECE RECEIVED: idx={} off={} len={}",
                 info_.endpoint.ToString(), piece_index, offset, data.size());
    info_.last_piece_time = std::chrono::steady_clock::now();
    info_.is_snubbed = false;

    auto result = piece_manager_.OnBlockReceived(piece_index, offset, data);

    switch (result) {
      case piece_manager::PieceManager::BlockResult::PieceVerified:
        // Broadcast Have to all peers
        on_piece_complete_(piece_index);
        break;
      case piece_manager::PieceManager::BlockResult::PieceFailed:
        on_piece_failed_(piece_index);
        break;
      default:
        break;
    }

    // Request more blocks
    RequestMoreBlocks();
  }

  void OnCancel(peer_wire::BlockInfo const& block) override {
    // Remove from our upload queue (if implemented)
    std::ignore = block;
  }

  // Public interface
  void RequestMoreBlocks() {
    if (!connection_->CanRequest()) {
      auto const& ps = connection_->GetPeerState();
      std::println(
          "[PROTO] {} - CanRequest=false (choking={}, interested={}, "
          "connected={})",
          info_.endpoint.ToString(), ps.peer_choking, ps.am_interested,
          connection_->IsConnected());
      return;
    }

    std::size_t slots = connection_->AvailableRequestSlots();
    std::println("[PROTO] {} - Requesting blocks, {} slots available",
                 info_.endpoint.ToString(), slots);
    auto blocks = piece_manager_.SelectBlocks(info_.bitfield, slots);
    std::println("[PROTO] {} - Selected {} blocks to request",
                 info_.endpoint.ToString(), blocks.size());

    for (auto const& block : blocks) {
      std::println("[PROTO] {} - REQUEST piece={} offset={} len={}",
                   info_.endpoint.ToString(), block.piece_index, block.offset,
                   block.length);
      connection_->SendRequest(block);
    }
  }

  void SetInterested(bool interested) {
    if (interested && !info_.state.am_interested) {
      std::println("[PROTO] {} - Sending INTERESTED",
                   info_.endpoint.ToString());
      connection_->SendInterested();
      info_.state.am_interested = true;
    } else if (!interested && info_.state.am_interested) {
      std::println("[PROTO] {} - Sending NOT_INTERESTED",
                   info_.endpoint.ToString());
      connection_->SendNotInterested();
      info_.state.am_interested = false;
    }
  }

  void Choke() {
    if (!info_.state.am_choking) {
      connection_->SendChoke();
      info_.state.am_choking = true;
    }
  }

  void Unchoke() {
    if (info_.state.am_choking) {
      connection_->SendUnchoke();
      info_.state.am_choking = false;
    }
  }

  void SendHave(std::uint32_t piece_index) {
    connection_->SendHave(piece_index);
  }

  void SendKeepAlive() {
    connection_->SendKeepAlive();
  }

  void Close() {
    connection_->Close();
  }

  [[nodiscard]] PeerInfo const& GetInfo() const noexcept { return info_; }
  [[nodiscard]] PeerInfo& GetInfo() noexcept { return info_; }

  [[nodiscard]] peer_wire::PeerEndpoint const& GetEndpoint() const noexcept {
    return info_.endpoint;
  }

  [[nodiscard]] bool IsConnected() const noexcept {
    return connection_->IsConnected();
  }

  [[nodiscard]] peer_wire::PeerStats GetStats() const noexcept {
    return connection_->GetStats();
  }

  [[nodiscard]] std::error_code GetDisconnectError() const noexcept {
    return disconnect_error_;
  }

  // Callbacks for manager
  std::function<void(std::uint32_t)> on_piece_complete_{};
  std::function<void(std::uint32_t)> on_piece_failed_{};

 private:
  void UpdateInterestState() {
    auto local_bf = piece_manager_.GetLocalBitfield();
    bool should_be_interested = info_.IsInteresting(local_bf);
    std::println("[PROTO] {} - UpdateInterest: should_be_interested={}",
                 info_.endpoint.ToString(), should_be_interested);
    SetInterested(should_be_interested);
  }

  std::shared_ptr<peer_wire::PeerConnection> connection_{};
  piece_manager::PieceManager& piece_manager_;
  std::function<void(ManagedPeer*)> on_disconnect_{};
  PeerInfo info_{};
  std::error_code disconnect_error_{};
};

// ============================================================================
// Peer Manager
// ============================================================================

export class PeerManager {
 public:
  PeerManager(torrent::Torrent const& torrent,
              piece_manager::PieceManager& piece_manager,
              peer_wire::PeerId local_peer_id,
              PeerManagerConfig config = {})
      : torrent_{torrent},
        piece_manager_{piece_manager},
        local_peer_id_{local_peer_id},
        config_{std::move(config)} {}

  ~PeerManager() { Stop(); }

  // Non-copyable
  PeerManager(PeerManager const&) = delete;
  PeerManager& operator=(PeerManager const&) = delete;

  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------

  void Start() {
    std::lock_guard lock{mutex_};
    if (running_) return;
    running_ = true;
  }

  void Stop() {
    std::lock_guard lock{mutex_};
    if (!running_) return;
    running_ = false;

    // Close all connections
    for (auto& [endpoint, peer] : peers_) {
      peer->Close();
    }
    peers_.clear();
  }

  [[nodiscard]] bool IsRunning() const {
    std::lock_guard lock{mutex_};
    return running_;
  }

  // -------------------------------------------------------------------------
  // Peer Management
  // -------------------------------------------------------------------------

  // Add peer from tracker/DHT/PEX
  bool AddPeer(peer_wire::PeerEndpoint const& endpoint) {
    std::lock_guard lock{mutex_};

    if (!running_ || peers_.size() >= config_.max_connections) {
      return false;
    }

    // Check if already connected
    std::string key = endpoint.ToString();
    if (peers_.contains(key)) {
      return false;
    }

    // Add to pending list for connection
    pending_peers_.push_back(endpoint);
    return true;
  }

  // Add multiple peers
  std::size_t AddPeers(std::vector<peer_wire::PeerEndpoint> const& endpoints) {
    std::size_t added{0};
    for (auto const& ep : endpoints) {
      if (AddPeer(ep)) {
        ++added;
      }
    }
    return added;
  }

  // Connect to a peer (called externally after socket creation)
  void OnPeerConnected(std::shared_ptr<peer_wire::PeerConnection> connection) {
    std::lock_guard lock{mutex_};

    if (!running_) {
      connection->Close();
      return;
    }

    auto endpoint = connection->GetEndpoint();
    std::string key = endpoint.ToString();

    if (peers_.contains(key) || peers_.size() >= config_.max_connections) {
      connection->Close();
      return;
    }

    auto peer = std::make_unique<ManagedPeer>(
        std::move(connection),
        piece_manager_,
        [this](ManagedPeer* p) { OnPeerDisconnectedInternal(p); });

    peer->on_piece_complete_ = [this](std::uint32_t idx) {
      OnPieceCompleteInternal(idx);
    };
    peer->on_piece_failed_ = [this](std::uint32_t idx) {
      OnPieceFailedInternal(idx);
    };

    peers_[key] = std::move(peer);

    if (handler_) {
      handler_->OnPeerConnected(peers_[key]->GetInfo());
    }
  }

  // Disconnect a specific peer
  void DisconnectPeer(peer_wire::PeerEndpoint const& endpoint) {
    std::lock_guard lock{mutex_};
    std::string key = endpoint.ToString();

    auto it = peers_.find(key);
    if (it != peers_.end()) {
      it->second->Close();
      // Will be removed in OnPeerDisconnectedInternal
    }
  }

  // -------------------------------------------------------------------------
  // Choking Algorithm
  // -------------------------------------------------------------------------

  // Run the choking algorithm (should be called periodically)
  void RunChokingAlgorithm() {
    std::lock_guard lock{mutex_};

    // Get all connected peers sorted by download rate
    std::vector<ManagedPeer*> peers_vec{};
    for (auto& [key, peer] : peers_) {
      if (peer->IsConnected()) {
        peers_vec.push_back(peer.get());
      }
    }

    if (peers_vec.empty()) return;

    // Sort by download rate (for leecher) or upload rate (for seeder)
    bool is_seeder = piece_manager_.IsComplete();

    if (is_seeder) {
      // Seeder: unchoke peers that download fastest from us
      std::ranges::sort(peers_vec, [](ManagedPeer* a, ManagedPeer* b) {
        return a->GetStats().bytes_uploaded > b->GetStats().bytes_uploaded;
      });
    } else {
      // Leecher: unchoke peers that upload fastest to us
      std::ranges::sort(peers_vec, [](ManagedPeer* a, ManagedPeer* b) {
        return a->GetStats().bytes_downloaded > b->GetStats().bytes_downloaded;
      });
    }

    // Choke all first
    for (auto* peer : peers_vec) {
      peer->Choke();
    }

    // Unchoke top N peers
    std::size_t unchoked{0};
    for (auto* peer : peers_vec) {
      if (unchoked >= config_.max_upload_slots) break;

      // Only unchoke interested peers
      if (peer->GetInfo().state.peer_interested) {
        peer->Unchoke();
        ++unchoked;
      }
    }

    // Optimistic unchoke: randomly unchoke one more peer
    if (config_.optimistic_unchoke_slots > 0 && peers_vec.size() > unchoked) {
      std::vector<ManagedPeer*> choked_interested{};
      for (auto* peer : peers_vec) {
        if (peer->GetInfo().state.am_choking &&
            peer->GetInfo().state.peer_interested) {
          choked_interested.push_back(peer);
        }
      }

      if (!choked_interested.empty()) {
        std::random_device rd{};
        std::mt19937 gen{rd()};
        std::uniform_int_distribution<std::size_t> dist{
            0, choked_interested.size() - 1};

        for (std::size_t i = 0; i < config_.optimistic_unchoke_slots &&
                                i < choked_interested.size();
             ++i) {
          std::size_t idx = dist(gen);
          choked_interested[idx]->Unchoke();
        }
      }
    }
  }

  // -------------------------------------------------------------------------
  // Queries
  // -------------------------------------------------------------------------

  [[nodiscard]] std::size_t ConnectionCount() const {
    std::lock_guard lock{mutex_};
    return peers_.size();
  }

  [[nodiscard]] std::size_t PendingConnectionCount() const {
    std::lock_guard lock{mutex_};
    return pending_peers_.size();
  }

  [[nodiscard]] std::vector<PeerInfo> GetPeerInfos() const {
    std::lock_guard lock{mutex_};
    std::vector<PeerInfo> result;
    result.reserve(peers_.size());
    for (auto const& [key, peer] : peers_) {
      result.push_back(peer->GetInfo());
    }
    return result;
  }

  [[nodiscard]] std::optional<PeerInfo> GetPeerInfo(
      peer_wire::PeerEndpoint const& endpoint) const {
    std::lock_guard lock{mutex_};
    std::string key = endpoint.ToString();
    auto it = peers_.find(key);
    if (it == peers_.end()) return std::nullopt;
    return it->second->GetInfo();
  }

  // Aggregate stats
  struct AggregateStats {
    std::uint64_t total_downloaded{0};
    std::uint64_t total_uploaded{0};
    double download_rate{0.0};
    double upload_rate{0.0};
    std::size_t connected_peers{0};
    std::size_t seeders{0};
    std::size_t leechers{0};
  };

  [[nodiscard]] AggregateStats GetAggregateStats() const {
    std::lock_guard lock{mutex_};
    AggregateStats stats{};
    stats.connected_peers = peers_.size();

    for (auto const& [key, peer] : peers_) {
      auto ps = peer->GetStats();
      stats.total_downloaded += ps.bytes_downloaded;
      stats.total_uploaded += ps.bytes_uploaded;
      stats.download_rate += ps.DownloadRate();
      stats.upload_rate += ps.UploadRate();

      if (peer->GetInfo().is_seed) {
        ++stats.seeders;
      } else {
        ++stats.leechers;
      }
    }

    return stats;
  }

  // Get next pending peer for connection
  [[nodiscard]] std::optional<peer_wire::PeerEndpoint> GetNextPendingPeer() {
    std::lock_guard lock{mutex_};
    if (pending_peers_.empty()) return std::nullopt;

    auto endpoint = pending_peers_.front();
    pending_peers_.pop_front();
    return endpoint;
  }

  // -------------------------------------------------------------------------
  // Broadcast
  // -------------------------------------------------------------------------

  void BroadcastHave(std::uint32_t piece_index) {
    std::lock_guard lock{mutex_};
    for (auto& [key, peer] : peers_) {
      if (peer->IsConnected()) {
        peer->SendHave(piece_index);
      }
    }
  }

  void SendKeepAlives() {
    std::lock_guard lock{mutex_};
    for (auto& [key, peer] : peers_) {
      if (peer->IsConnected()) {
        peer->SendKeepAlive();
      }
    }
  }

  void ProcessPendingRemovals() {
    std::lock_guard lock{mutex_};
    CleanupRemovedPeers();
  }

  // -------------------------------------------------------------------------
  // Handler
  // -------------------------------------------------------------------------

  void SetHandler(IPeerManagerHandler* handler) { handler_ = handler; }

  // -------------------------------------------------------------------------
  // Configuration
  // -------------------------------------------------------------------------

  [[nodiscard]] PeerManagerConfig const& GetConfig() const noexcept {
    return config_;
  }

  void SetMaxConnections(std::size_t max) {
    std::lock_guard lock{mutex_};
    config_.max_connections = max;
  }

  void SetMaxUploadSlots(std::size_t max) {
    std::lock_guard lock{mutex_};
    config_.max_upload_slots = max;
  }

 private:
  void OnPeerDisconnectedInternal(ManagedPeer* peer) {
    // Note: Called from peer callback, already under operation
    auto endpoint = peer->GetEndpoint();
    auto ec = peer->GetDisconnectError();

    // Schedule removal (can't modify map during iteration)
    peers_to_remove_.push_back(endpoint.ToString());

    if (handler_) {
      handler_->OnPeerDisconnected(endpoint, ec);
    }
  }

  void OnPieceCompleteInternal(std::uint32_t piece_index) {
    // Broadcast Have to all peers
    BroadcastHave(piece_index);

    if (handler_) {
      handler_->OnPieceComplete(piece_index);
    }

    // Check if download complete
    if (piece_manager_.IsComplete()) {
      if (handler_) {
        handler_->OnDownloadComplete();
      }
    }
  }

  void OnPieceFailedInternal(std::uint32_t piece_index) {
    if (handler_) {
      handler_->OnPieceFailed(piece_index);
    }
  }

  void CleanupRemovedPeers() {
    for (auto const& key : peers_to_remove_) {
      peers_.erase(key);
    }
    peers_to_remove_.clear();
  }

  torrent::Torrent const& torrent_;
  piece_manager::PieceManager& piece_manager_;
  peer_wire::PeerId local_peer_id_;
  PeerManagerConfig config_{};
  IPeerManagerHandler* handler_{nullptr};

  mutable std::mutex mutex_{};
  bool running_{false};
  std::map<std::string, std::unique_ptr<ManagedPeer>> peers_{};
  std::deque<peer_wire::PeerEndpoint> pending_peers_{};
  std::vector<std::string> peers_to_remove_{};
};

// ============================================================================
// Utility Functions
// ============================================================================

// Calculate estimated time remaining
export [[nodiscard]] std::chrono::seconds EstimateTimeRemaining(
    std::uint64_t remaining_bytes, double download_rate) {
  if (download_rate <= 0.0 || remaining_bytes == 0) {
    return std::chrono::seconds::max();
  }
  return std::chrono::seconds{
      static_cast<std::int64_t>(remaining_bytes / download_rate)};
}

// Format bytes as human-readable string
export [[nodiscard]] std::string FormatBytes(std::uint64_t bytes) {
  constexpr std::array<char const*, 5> units{"B", "KB", "MB", "GB", "TB"};
  double size = static_cast<double>(bytes);
  std::size_t unit_idx{0};

  while (size >= 1024.0 && unit_idx < units.size() - 1) {
    size /= 1024.0;
    ++unit_idx;
  }

  return std::format("{:.2f} {}", size, units[unit_idx]);
}

// Format rate as human-readable string
export [[nodiscard]] std::string FormatRate(double bytes_per_second) {
  return FormatBytes(static_cast<std::uint64_t>(bytes_per_second)) + "/s";
}

}  // namespace byte_torrent::peer_manager
