export module piece_manager;

export import :types;
export import :selector;

import std;
import torrent;
import peer_wire;

namespace byte_torrent::piece_manager {

// ============================================================================
// Piece Manager Configuration
// ============================================================================

export struct PieceManagerConfig {
  std::uint32_t block_size{peer_wire::kBlockSize};
  std::size_t max_pending_requests_per_peer{5};
  std::size_t endgame_threshold{10};  // Switch to endgame when this many pieces left
  SelectionStrategy selection_strategy{SelectionStrategy::RarestFirst};
  bool enable_endgame{true};
};

// ============================================================================
// Piece Manager
// ============================================================================

export class PieceManager {
 public:
  // Construct from torrent info
  explicit PieceManager(torrent::Info const& info,
                        PieceManagerConfig config = {})
      : config_{std::move(config)},
        piece_length_{static_cast<std::uint32_t>(info.piece_length)},
        total_length_{info.TotalLength()},
        piece_hashes_{info.pieces} {
    
    InitializePieces();
    availability_.Resize(pieces_.size());
    selector_ = CreateSelector(config_.selection_strategy, availability_);
    
    stats_.total_pieces = pieces_.size();
    stats_.total_bytes = total_length_;
  }

  // -------------------------------------------------------------------------
  // Piece Selection
  // -------------------------------------------------------------------------

  // Select a block to request from a peer
  [[nodiscard]] std::optional<peer_wire::BlockInfo> SelectBlock(
      peer_wire::Bitfield const& peer_bitfield) {
    
    std::lock_guard lock{mutex_};

    // First, try to get a block from pieces already in progress
    for (auto& piece : pieces_) {
      if (piece.State() == PieceState::Downloading &&
          peer_bitfield.HasPiece(piece.PieceIndex())) {
        if (auto block = piece.GetNextAvailableBlock()) {
          piece.MarkBlockRequested(block->offset);
          return block;
        }
      }
    }

    // Select new piece using strategy
    auto const piece_idx = selector_->SelectPiece(
        peer_bitfield,
        [this](std::uint32_t idx) { return GetPieceStateUnlocked(idx); },
        [this](std::uint32_t idx) { return GetPiecePriorityUnlocked(idx); });

    if (!piece_idx) {
      // Check for endgame mode
      if (config_.enable_endgame && ShouldEnterEndgame()) {
        return SelectEndgameBlock(peer_bitfield);
      }
      return std::nullopt;
    }

    // Get block from selected piece
    auto& piece = pieces_[*piece_idx];
    if (auto block = piece.GetNextAvailableBlock()) {
      piece.MarkBlockRequested(block->offset);
      return block;
    }

    return std::nullopt;
  }

  // Select multiple blocks (for pipelining)
  [[nodiscard]] std::vector<peer_wire::BlockInfo> SelectBlocks(
      peer_wire::Bitfield const& peer_bitfield,
      std::size_t max_count) {
    
    std::vector<peer_wire::BlockInfo> blocks{};
    blocks.reserve(max_count);

    for (std::size_t i = 0; i < max_count; ++i) {
      if (auto block = SelectBlock(peer_bitfield)) {
        blocks.push_back(*block);
      } else {
        break;
      }
    }

    return blocks;
  }

  // -------------------------------------------------------------------------
  // Block Reception
  // -------------------------------------------------------------------------

  // Receive a block from a peer
  enum class BlockResult {
    Accepted,       // Block stored successfully
    PieceComplete,  // Block stored and piece is now complete
    PieceVerified,  // Block stored and piece verified successfully
    PieceFailed,    // Piece complete but verification failed
    Duplicate,      // Block already received
    Invalid         // Invalid block (wrong size, offset, etc.)
  };

  [[nodiscard]] BlockResult OnBlockReceived(
      std::uint32_t piece_index,
      std::uint32_t offset,
      std::span<std::byte const> data) {
    
    std::lock_guard lock{mutex_};

    if (piece_index >= pieces_.size()) {
      return BlockResult::Invalid;
    }

    auto& piece = pieces_[piece_index];

    // Check if already received
    std::size_t const block_idx{piece.BlockIndex(offset)};
    if (block_idx < piece.BlockCount() &&
        piece.GetBlockState(block_idx).received) {
      return BlockResult::Duplicate;
    }

    // Store the block
    if (!piece.StoreBlock(offset, data)) {
      return BlockResult::Invalid;
    }

    stats_.downloaded_bytes += data.size();

    // Check if piece is complete
    if (!piece.IsComplete()) {
      return BlockResult::Accepted;
    }

    // Verify the piece
    if (VerifyPieceUnlocked(piece_index)) {
      piece.SetVerified();
      ++stats_.verified_pieces;
      UpdateEndgameState();

      // Update selector if hybrid
      if (auto* hybrid = dynamic_cast<HybridSelector*>(selector_.get())) {
        hybrid->SetCompletedPieceCount(stats_.verified_pieces);
      }

      return BlockResult::PieceVerified;
    }

    // Verification failed - reset piece
    stats_.wasted_bytes += piece.PieceLength();
    piece.Reset();
    return BlockResult::PieceFailed;
  }

  // -------------------------------------------------------------------------
  // Request Management
  // -------------------------------------------------------------------------

  // Cancel a pending request (e.g., peer disconnected)
  void CancelRequest(peer_wire::BlockInfo const& block) {
    std::lock_guard lock{mutex_};
    if (block.piece_index < pieces_.size()) {
      pieces_[block.piece_index].CancelBlockRequest(block.offset);
    }
  }

  // Cancel all pending requests for a peer
  void CancelAllRequests(std::vector<peer_wire::BlockInfo> const& blocks) {
    std::lock_guard lock{mutex_};
    for (auto const& block : blocks) {
      if (block.piece_index < pieces_.size()) {
        pieces_[block.piece_index].CancelBlockRequest(block.offset);
      }
    }
  }

  // -------------------------------------------------------------------------
  // Availability Tracking
  // -------------------------------------------------------------------------

  // Update availability when peer connects with bitfield
  void OnPeerBitfield(peer_wire::Bitfield const& bitfield) {
    std::lock_guard lock{mutex_};
    availability_.AddPeer(bitfield);
  }

  // Update availability when peer disconnects
  void OnPeerDisconnected(peer_wire::Bitfield const& bitfield) {
    std::lock_guard lock{mutex_};
    availability_.RemovePeer(bitfield);
  }

  // Update availability when peer sends Have message
  void OnPeerHave(std::uint32_t piece_index) {
    std::lock_guard lock{mutex_};
    availability_.IncrementPiece(piece_index);
  }

  // -------------------------------------------------------------------------
  // State Queries
  // -------------------------------------------------------------------------

  [[nodiscard]] PieceState GetPieceState(std::uint32_t piece_index) const {
    std::lock_guard lock{mutex_};
    return GetPieceStateUnlocked(piece_index);
  }

  [[nodiscard]] PiecePriority GetPiecePriority(std::uint32_t piece_index) const {
    std::lock_guard lock{mutex_};
    return GetPiecePriorityUnlocked(piece_index);
  }

  void SetPiecePriority(std::uint32_t piece_index, PiecePriority priority) {
    std::lock_guard lock{mutex_};
    if (piece_index < pieces_.size()) {
      pieces_[piece_index].SetPriority(priority);
    }
  }

  // Get our current bitfield (for sending to peers)
  [[nodiscard]] peer_wire::Bitfield GetLocalBitfield() const {
    std::lock_guard lock{mutex_};
    peer_wire::Bitfield bitfield{pieces_.size()};
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
      if (pieces_[i].State() == PieceState::Verified) {
        bitfield.SetPiece(i);
      }
    }
    return bitfield;
  }

  // Check if we have a specific piece
  [[nodiscard]] bool HasPiece(std::uint32_t piece_index) const {
    std::lock_guard lock{mutex_};
    if (piece_index >= pieces_.size()) return false;
    return pieces_[piece_index].State() == PieceState::Verified;
  }

  // Get verified piece data (for uploading to peers)
  [[nodiscard]] std::optional<std::span<std::byte const>> GetPieceData(
      std::uint32_t piece_index) const {
    std::lock_guard lock{mutex_};
    if (piece_index >= pieces_.size()) return std::nullopt;
    if (pieces_[piece_index].State() != PieceState::Verified) return std::nullopt;
    return pieces_[piece_index].Data();
  }

  // -------------------------------------------------------------------------
  // Statistics
  // -------------------------------------------------------------------------

  [[nodiscard]] DownloadStats GetStats() const {
    std::lock_guard lock{mutex_};
    return stats_;
  }

  [[nodiscard]] double GetProgress() const {
    std::lock_guard lock{mutex_};
    return stats_.Progress();
  }

  [[nodiscard]] bool IsComplete() const {
    std::lock_guard lock{mutex_};
    return stats_.IsComplete();
  }

  [[nodiscard]] std::size_t PieceCount() const noexcept {
    return pieces_.size();
  }

  [[nodiscard]] std::uint32_t PieceLength() const noexcept {
    return piece_length_;
  }

  [[nodiscard]] std::uint64_t TotalLength() const noexcept {
    return total_length_;
  }

  // Get remaining pieces count
  [[nodiscard]] std::size_t RemainingPieces() const {
    std::lock_guard lock{mutex_};
    return stats_.total_pieces - stats_.verified_pieces;
  }

  // -------------------------------------------------------------------------
  // Endgame Mode
  // -------------------------------------------------------------------------

  [[nodiscard]] bool IsInEndgame() const {
    std::lock_guard lock{mutex_};
    return in_endgame_;
  }

  // Get all pending blocks (for endgame cancellation)
  [[nodiscard]] std::vector<peer_wire::BlockInfo> GetPendingBlocks() const {
    std::lock_guard lock{mutex_};
    std::vector<peer_wire::BlockInfo> pending{};

    for (auto const& piece : pieces_) {
      if (piece.State() == PieceState::Downloading) {
        for (std::size_t i = 0; i < piece.BlockCount(); ++i) {
          if (piece.GetBlockState(i).IsPending()) {
            pending.push_back({.piece_index = piece.PieceIndex(),
                               .offset = piece.BlockOffset(i),
                               .length = piece.BlockLength(i)});
          }
        }
      }
    }

    return pending;
  }

 private:
  void InitializePieces() {
    std::size_t const piece_count{piece_hashes_.size()};
    pieces_.reserve(piece_count);

    for (std::size_t i = 0; i < piece_count; ++i) {
      // Last piece may be smaller
      std::uint64_t const piece_start{i * piece_length_};
      std::uint32_t const this_piece_length{
          static_cast<std::uint32_t>(std::min(
              static_cast<std::uint64_t>(piece_length_),
              total_length_ - piece_start))};

      pieces_.emplace_back(static_cast<std::uint32_t>(i), this_piece_length,
                           config_.block_size);
    }
  }

  [[nodiscard]] PieceState GetPieceStateUnlocked(std::uint32_t piece_index) const {
    if (piece_index >= pieces_.size()) return PieceState::Missing;
    return pieces_[piece_index].State();
  }

  [[nodiscard]] PiecePriority GetPiecePriorityUnlocked(std::uint32_t piece_index) const {
    if (piece_index >= pieces_.size()) return PiecePriority::Skip;
    return pieces_[piece_index].Priority();
  }

  [[nodiscard]] bool VerifyPieceUnlocked(std::uint32_t piece_index) const {
    if (piece_index >= pieces_.size() || piece_index >= piece_hashes_.size()) {
      return false;
    }

    auto const& piece = pieces_[piece_index];
    auto const data = piece.Data();
    auto const computed_hash = torrent::CalculateSha1(data);

    return computed_hash == piece_hashes_[piece_index];
  }

  [[nodiscard]] bool ShouldEnterEndgame() const {
    if (in_endgame_) return true;
    return RemainingPiecesUnlocked() <= config_.endgame_threshold;
  }

  [[nodiscard]] std::size_t RemainingPiecesUnlocked() const {
    return stats_.total_pieces - stats_.verified_pieces;
  }

  void UpdateEndgameState() {
    if (!in_endgame_ && config_.enable_endgame) {
      in_endgame_ = RemainingPiecesUnlocked() <= config_.endgame_threshold;
    }
  }

  // Endgame: request blocks that are pending (already requested from other peers)
  [[nodiscard]] std::optional<peer_wire::BlockInfo> SelectEndgameBlock(
      peer_wire::Bitfield const& peer_bitfield) {
    
    for (auto& piece : pieces_) {
      if ((piece.State() == PieceState::Downloading ||
           piece.State() == PieceState::Missing) &&
          peer_bitfield.HasPiece(piece.PieceIndex())) {
        if (auto block = piece.GetNextUnreceivedBlock()) {
          piece.MarkBlockRequested(block->offset);
          return block;
        }
      }
    }

    return std::nullopt;
  }

  PieceManagerConfig config_{};
  std::uint32_t piece_length_{};
  std::uint64_t total_length_{};
  std::vector<torrent::Sha1Hash> piece_hashes_{};
  std::vector<PieceInfo> pieces_{};
  PieceAvailability availability_{};
  std::unique_ptr<IPieceSelector> selector_{};
  DownloadStats stats_{};
  bool in_endgame_{false};
  mutable std::mutex mutex_{};
};

// ============================================================================
// Utility Functions
// ============================================================================

// Calculate which pieces are needed for a byte range (for streaming/partial download)
export [[nodiscard]] std::vector<std::uint32_t> GetPiecesForRange(
    std::uint64_t start_byte,
    std::uint64_t end_byte,
    std::uint32_t piece_length) {
  
  if (piece_length == 0 || end_byte <= start_byte) {
    return {};
  }

  std::uint32_t const start_piece{
      static_cast<std::uint32_t>(start_byte / piece_length)};
  std::uint32_t const end_piece{
      static_cast<std::uint32_t>((end_byte - 1) / piece_length)};

  std::vector<std::uint32_t> pieces{};
  pieces.reserve(end_piece - start_piece + 1);

  for (std::uint32_t i = start_piece; i <= end_piece; ++i) {
    pieces.push_back(i);
  }

  return pieces;
}

// Calculate piece boundaries for a file in a multi-file torrent
export struct FilePieceMapping {
  std::uint32_t first_piece{};
  std::uint32_t last_piece{};
  std::uint32_t first_piece_offset{};  // Offset within first piece
  std::uint32_t last_piece_length{};   // Length within last piece
};

export [[nodiscard]] FilePieceMapping MapFileToPieces(
    std::uint64_t file_offset,
    std::uint64_t file_length,
    std::uint32_t piece_length) {
  
  if (piece_length == 0 || file_length == 0) {
    return {};
  }

  FilePieceMapping mapping{};
  mapping.first_piece = static_cast<std::uint32_t>(file_offset / piece_length);
  mapping.first_piece_offset =
      static_cast<std::uint32_t>(file_offset % piece_length);

  std::uint64_t const end_byte{file_offset + file_length - 1};
  mapping.last_piece = static_cast<std::uint32_t>(end_byte / piece_length);
  mapping.last_piece_length =
      static_cast<std::uint32_t>((end_byte % piece_length) + 1);

  return mapping;
}

}  // namespace byte_torrent::piece_manager
