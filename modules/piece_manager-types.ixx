export module piece_manager:types;

import std;
import peer_wire;

namespace byte_torrent::piece_manager {

// ============================================================================
// Piece State
// ============================================================================

export enum class PieceState : std::uint8_t {
  Missing,      // Not downloaded, no blocks requested
  Downloading,  // Some blocks requested or received
  Complete,     // All blocks received (pending verification)
  Verified      // Hash verified successfully
};

export [[nodiscard]] constexpr std::string_view ToString(
    PieceState state) noexcept {
  switch (state) {
    case PieceState::Missing:
      return "Missing";
    case PieceState::Downloading:
      return "Downloading";
    case PieceState::Complete:
      return "Complete";
    case PieceState::Verified:
      return "Verified";
    default:
      return "Unknown";
  }
}

// ============================================================================
// Block State
// ============================================================================

export struct BlockState {
  bool requested{false};  // Block has been requested from a peer
  bool received{false};   // Block data has been received

  [[nodiscard]] constexpr bool IsPending() const noexcept {
    return requested && !received;
  }

  [[nodiscard]] constexpr bool IsComplete() const noexcept { return received; }

  [[nodiscard]] constexpr bool IsAvailable() const noexcept {
    return !requested && !received;
  }

  void Reset() noexcept {
    requested = false;
    received = false;
  }

  [[nodiscard]] friend constexpr bool operator==(BlockState const&,
                                                  BlockState const&) = default;
};

// ============================================================================
// Piece Priority
// ============================================================================

export enum class PiecePriority : std::uint8_t {
  Skip = 0,      // Don't download this piece
  Low = 1,       // Download after normal priority
  Normal = 2,    // Default priority
  High = 3,      // Download before normal priority
  Critical = 4   // Download as soon as possible (e.g., first/last pieces)
};

export [[nodiscard]] constexpr std::string_view ToString(
    PiecePriority priority) noexcept {
  switch (priority) {
    case PiecePriority::Skip:
      return "Skip";
    case PiecePriority::Low:
      return "Low";
    case PiecePriority::Normal:
      return "Normal";
    case PiecePriority::High:
      return "High";
    case PiecePriority::Critical:
      return "Critical";
    default:
      return "Unknown";
  }
}

// ============================================================================
// Piece Info (runtime state for a single piece)
// ============================================================================

export class PieceInfo {
 public:
  PieceInfo() = default;

  explicit PieceInfo(std::uint32_t piece_index, std::uint32_t piece_length,
                     std::uint32_t block_size = peer_wire::kBlockSize)
      : piece_index_{piece_index},
        piece_length_{piece_length},
        block_size_{block_size} {
    std::uint32_t const block_count{(piece_length + block_size - 1) / block_size};
    blocks_.resize(block_count);
    data_.resize(piece_length);
  }

  // Accessors
  [[nodiscard]] std::uint32_t PieceIndex() const noexcept { return piece_index_; }
  [[nodiscard]] std::uint32_t PieceLength() const noexcept { return piece_length_; }
  [[nodiscard]] std::uint32_t BlockSize() const noexcept { return block_size_; }
  [[nodiscard]] std::size_t BlockCount() const noexcept { return blocks_.size(); }
  [[nodiscard]] PieceState State() const noexcept { return state_; }
  [[nodiscard]] PiecePriority Priority() const noexcept { return priority_; }

  // Block indexing
  [[nodiscard]] std::size_t BlockIndex(std::uint32_t offset) const noexcept {
    return offset / block_size_;
  }

  [[nodiscard]] std::uint32_t BlockOffset(std::size_t block_index) const noexcept {
    return static_cast<std::uint32_t>(block_index * block_size_);
  }

  [[nodiscard]] std::uint32_t BlockLength(std::size_t block_index) const noexcept {
    std::uint32_t const offset{BlockOffset(block_index)};
    return std::min(block_size_, piece_length_ - offset);
  }

  // Block state access
  [[nodiscard]] BlockState const& GetBlockState(std::size_t index) const {
    return blocks_.at(index);
  }

  [[nodiscard]] BlockState& GetBlockState(std::size_t index) {
    return blocks_.at(index);
  }

  // Find next available block for requesting
  [[nodiscard]] std::optional<peer_wire::BlockInfo> GetNextAvailableBlock() const {
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
      if (blocks_[i].IsAvailable()) {
        return peer_wire::BlockInfo{.piece_index = piece_index_,
                                    .offset = BlockOffset(i),
                                    .length = BlockLength(i)};
      }
    }
    return std::nullopt;
  }

  // Find next unreceived block (for endgame mode - request even if pending)
  [[nodiscard]] std::optional<peer_wire::BlockInfo> GetNextUnreceivedBlock() const {
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
      if (!blocks_[i].received) {
        return peer_wire::BlockInfo{.piece_index = piece_index_,
                                    .offset = BlockOffset(i),
                                    .length = BlockLength(i)};
      }
    }
    return std::nullopt;
  }

  // Mark block as requested
  void MarkBlockRequested(std::uint32_t offset) {
    std::size_t const idx{BlockIndex(offset)};
    if (idx < blocks_.size()) {
      blocks_[idx].requested = true;
      UpdateState();
    }
  }

  // Store received block data
  bool StoreBlock(std::uint32_t offset, std::span<std::byte const> block_data) {
    std::size_t const idx{BlockIndex(offset)};
    if (idx >= blocks_.size()) {
      return false;
    }

    std::uint32_t const expected_len{BlockLength(idx)};
    if (block_data.size() != expected_len) {
      return false;
    }

    if (offset + block_data.size() > data_.size()) {
      return false;
    }

    // Copy data
    std::copy(block_data.begin(), block_data.end(), data_.begin() + offset);

    blocks_[idx].received = true;
    blocks_[idx].requested = true;  // Ensure consistency

    UpdateState();
    return true;
  }

  // Cancel a pending request
  void CancelBlockRequest(std::uint32_t offset) {
    std::size_t const idx{BlockIndex(offset)};
    if (idx < blocks_.size() && !blocks_[idx].received) {
      blocks_[idx].requested = false;
      UpdateState();
    }
  }

  // Reset all block states (e.g., after hash failure)
  void Reset() {
    for (auto& block : blocks_) {
      block.Reset();
    }
    state_ = PieceState::Missing;
  }

  // Set piece as verified
  void SetVerified() { state_ = PieceState::Verified; }

  // Set priority
  void SetPriority(PiecePriority priority) { priority_ = priority; }

  // Get piece data (for verification or writing to disk)
  [[nodiscard]] std::span<std::byte const> Data() const noexcept {
    return data_;
  }

  [[nodiscard]] std::span<std::byte> Data() noexcept { return data_; }

  // Statistics
  [[nodiscard]] std::size_t ReceivedBlockCount() const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(blocks_, [](auto const& b) { return b.received; }));
  }

  [[nodiscard]] std::size_t PendingBlockCount() const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(blocks_, [](auto const& b) { return b.IsPending(); }));
  }

  [[nodiscard]] double Progress() const noexcept {
    if (blocks_.empty()) return 0.0;
    return static_cast<double>(ReceivedBlockCount()) /
           static_cast<double>(blocks_.size());
  }

  [[nodiscard]] bool IsComplete() const noexcept {
    return std::ranges::all_of(blocks_,
                               [](auto const& b) { return b.received; });
  }

 private:
  void UpdateState() {
    if (IsComplete()) {
      state_ = PieceState::Complete;
    } else if (std::ranges::any_of(blocks_, [](auto const& b) {
                 return b.requested || b.received;
               })) {
      state_ = PieceState::Downloading;
    } else {
      state_ = PieceState::Missing;
    }
  }

  std::uint32_t piece_index_{0};
  std::uint32_t piece_length_{0};
  std::uint32_t block_size_{peer_wire::kBlockSize};
  PieceState state_{PieceState::Missing};
  PiecePriority priority_{PiecePriority::Normal};
  std::vector<BlockState> blocks_{};
  std::vector<std::byte> data_{};
};

// ============================================================================
// Piece Selection Strategy
// ============================================================================

export enum class SelectionStrategy {
  Sequential,   // Download pieces in order (for streaming)
  RarestFirst,  // Download rarest pieces first (default BitTorrent)
  Random        // Random selection (for initial pieces)
};

// ============================================================================
// Download Statistics
// ============================================================================

export struct DownloadStats {
  std::uint64_t total_pieces{0};
  std::uint64_t verified_pieces{0};
  std::uint64_t downloaded_bytes{0};
  std::uint64_t total_bytes{0};
  std::uint64_t wasted_bytes{0};  // From hash failures

  [[nodiscard]] double Progress() const noexcept {
    if (total_bytes == 0) return 0.0;
    return static_cast<double>(downloaded_bytes) /
           static_cast<double>(total_bytes);
  }

  [[nodiscard]] double PieceProgress() const noexcept {
    if (total_pieces == 0) return 0.0;
    return static_cast<double>(verified_pieces) /
           static_cast<double>(total_pieces);
  }

  [[nodiscard]] bool IsComplete() const noexcept {
    return verified_pieces == total_pieces;
  }
};

// ============================================================================
// Peer Piece Availability
// ============================================================================

export class PieceAvailability {
 public:
  PieceAvailability() = default;

  explicit PieceAvailability(std::size_t piece_count)
      : availability_(piece_count, 0) {}

  // Add peer's pieces to availability count
  void AddPeer(peer_wire::Bitfield const& bitfield) {
    EnsureSize(bitfield.BitCount());
    for (std::size_t i = 0; i < availability_.size(); ++i) {
      if (bitfield.HasPiece(i)) {
        ++availability_[i];
      }
    }
  }

  // Remove peer's pieces from availability count
  void RemovePeer(peer_wire::Bitfield const& bitfield) {
    for (std::size_t i = 0; i < std::min(availability_.size(), bitfield.BitCount());
         ++i) {
      if (bitfield.HasPiece(i) && availability_[i] > 0) {
        --availability_[i];
      }
    }
  }

  // Increment availability for single piece (from Have message)
  void IncrementPiece(std::size_t piece_index) {
    EnsureSize(piece_index + 1);
    ++availability_[piece_index];
  }

  // Get availability count for a piece
  [[nodiscard]] std::uint32_t GetAvailability(std::size_t piece_index) const noexcept {
    if (piece_index >= availability_.size()) return 0;
    return availability_[piece_index];
  }

  // Find rarest pieces (lowest non-zero availability)
  [[nodiscard]] std::vector<std::size_t> GetRarestPieces(
      std::size_t max_count = 10) const {
    if (availability_.empty()) return {};

    // Find minimum non-zero availability
    std::uint32_t min_avail{std::numeric_limits<std::uint32_t>::max()};
    for (std::uint32_t const avail : availability_) {
      if (avail > 0 && avail < min_avail) {
        min_avail = avail;
      }
    }

    if (min_avail == std::numeric_limits<std::uint32_t>::max()) {
      return {};
    }

    // Collect pieces with minimum availability
    std::vector<std::size_t> rarest{};
    for (std::size_t i = 0; i < availability_.size() && rarest.size() < max_count;
         ++i) {
      if (availability_[i] == min_avail) {
        rarest.push_back(i);
      }
    }

    return rarest;
  }

  [[nodiscard]] std::size_t PieceCount() const noexcept {
    return availability_.size();
  }

  void Clear() { availability_.clear(); }

  void Resize(std::size_t piece_count) { availability_.resize(piece_count, 0); }

 private:
  void EnsureSize(std::size_t required_size) {
    if (availability_.size() < required_size) {
      availability_.resize(required_size, 0);
    }
  }

  std::vector<std::uint32_t> availability_{};
};

}  // namespace byte_torrent::piece_manager
