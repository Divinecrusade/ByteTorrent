export module piece_manager:selector;

import std;
import peer_wire;
import :types;

namespace byte_torrent::piece_manager {

// ============================================================================
// Piece Selector Interface
// ============================================================================

export class IPieceSelector {
 public:
  virtual ~IPieceSelector() = default;

  // Select next piece to download from pieces available from peer
  [[nodiscard]] virtual std::optional<std::uint32_t> SelectPiece(
      peer_wire::Bitfield const& peer_bitfield,
      std::function<PieceState(std::uint32_t)> const& get_state,
      std::function<PiecePriority(std::uint32_t)> const& get_priority) = 0;
};

// ============================================================================
// Sequential Selector (for streaming)
// ============================================================================

export class SequentialSelector final : public IPieceSelector {
 public:
  [[nodiscard]] std::optional<std::uint32_t> SelectPiece(
      peer_wire::Bitfield const& peer_bitfield,
      std::function<PieceState(std::uint32_t)> const& get_state,
      std::function<PiecePriority(std::uint32_t)> const& get_priority) override {
    // Use actual piece count, not bitfield size (bitfield may have padding)
    std::size_t const piece_count{peer_bitfield.CountPieces()};

    // First pass: find critical priority pieces
    for (std::uint32_t i = 0; i < piece_count; ++i) {
      if (peer_bitfield.HasPiece(i) && 
          get_state(i) == PieceState::Missing &&
          get_priority(i) == PiecePriority::Critical) {
        return i;
      }
    }

    // Second pass: sequential order, respecting priority > Skip
    for (std::uint32_t i = 0; i < piece_count; ++i) {
      if (peer_bitfield.HasPiece(i) && 
          get_state(i) == PieceState::Missing &&
          get_priority(i) > PiecePriority::Skip) {
        return i;
      }
    }

    return std::nullopt;
  }
};

// ============================================================================
// Rarest First Selector (default BitTorrent strategy)
// ============================================================================

export class RarestFirstSelector final : public IPieceSelector {
 public:
  explicit RarestFirstSelector(PieceAvailability& availability)
      : availability_{availability} {}

  [[nodiscard]] std::optional<std::uint32_t> SelectPiece(
      peer_wire::Bitfield const& peer_bitfield,
      std::function<PieceState(std::uint32_t)> const& get_state,
      std::function<PiecePriority(std::uint32_t)> const& get_priority) override {
    
    std::size_t const piece_count{peer_bitfield.BitCount()};

    // Build candidate list: pieces we need that peer has
    std::vector<std::uint32_t> candidates{};
    candidates.reserve(piece_count);

    for (std::uint32_t i = 0; i < piece_count; ++i) {
      if (peer_bitfield.HasPiece(i) && 
          get_state(i) == PieceState::Missing &&
          get_priority(i) > PiecePriority::Skip) {
        candidates.push_back(i);
      }
    }

    if (candidates.empty()) {
      return std::nullopt;
    }

    // Sort by: priority (descending), then availability (ascending)
    std::ranges::sort(candidates, [&](std::uint32_t a, std::uint32_t b) {
      auto const prio_a = get_priority(a);
      auto const prio_b = get_priority(b);
      if (prio_a != prio_b) {
        return prio_a > prio_b;  // Higher priority first
      }
      return availability_.GetAvailability(a) < availability_.GetAvailability(b);
    });

    // Return rarest piece with highest priority
    return candidates.front();
  }

 private:
  PieceAvailability& availability_;
};

// ============================================================================
// Random Selector (for initial pieces to improve piece diversity)
// ============================================================================

export class RandomSelector final : public IPieceSelector {
 public:
  [[nodiscard]] std::optional<std::uint32_t> SelectPiece(
      peer_wire::Bitfield const& peer_bitfield,
      std::function<PieceState(std::uint32_t)> const& get_state,
      std::function<PiecePriority(std::uint32_t)> const& get_priority) override {
    
    std::size_t const piece_count{peer_bitfield.BitCount()};

    // Build candidate list
    std::vector<std::uint32_t> candidates{};
    for (std::uint32_t i = 0; i < piece_count; ++i) {
      if (peer_bitfield.HasPiece(i) && 
          get_state(i) == PieceState::Missing &&
          get_priority(i) > PiecePriority::Skip) {
        candidates.push_back(i);
      }
    }

    if (candidates.empty()) {
      return std::nullopt;
    }

    // Random selection
    std::random_device rd{};
    std::mt19937 gen{rd()};
    std::uniform_int_distribution<std::size_t> dist{0, candidates.size() - 1};

    return candidates[dist(gen)];
  }
};

// ============================================================================
// Hybrid Selector (random for first N pieces, then rarest-first)
// ============================================================================

export class HybridSelector final : public IPieceSelector {
 public:
  HybridSelector(PieceAvailability& availability,
                 std::size_t random_threshold = 4)
      : availability_{availability},
        random_threshold_{random_threshold},
        random_selector_{},
        rarest_selector_{availability} {}

  void SetCompletedPieceCount(std::size_t count) { completed_pieces_ = count; }

  [[nodiscard]] std::optional<std::uint32_t> SelectPiece(
      peer_wire::Bitfield const& peer_bitfield,
      std::function<PieceState(std::uint32_t)> const& get_state,
      std::function<PiecePriority(std::uint32_t)> const& get_priority) override {
    
    // Use random selection for first N pieces to improve diversity
    if (completed_pieces_ < random_threshold_) {
      return random_selector_.SelectPiece(peer_bitfield, get_state, get_priority);
    }

    // Switch to rarest-first for remaining pieces
    return rarest_selector_.SelectPiece(peer_bitfield, get_state, get_priority);
  }

 private:
  PieceAvailability& availability_;
  std::size_t random_threshold_{};
  std::size_t completed_pieces_{0};
  RandomSelector random_selector_{};
  RarestFirstSelector rarest_selector_;
};

// ============================================================================
// Selector Factory
// ============================================================================

export [[nodiscard]] std::unique_ptr<IPieceSelector> CreateSelector(
    SelectionStrategy strategy, PieceAvailability& availability) {
  switch (strategy) {
    case SelectionStrategy::Sequential:
      return std::make_unique<SequentialSelector>();
    case SelectionStrategy::RarestFirst:
      return std::make_unique<RarestFirstSelector>(availability);
    case SelectionStrategy::Random:
      return std::make_unique<RandomSelector>();
    default:
      return std::make_unique<RarestFirstSelector>(availability);
  }
}

}  // namespace byte_torrent::piece_manager
