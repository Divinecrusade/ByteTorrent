export module peer_wire:buffer;

import std;
import :types;
import :message;

namespace byte_torrent::peer_wire {

// ============================================================================
// Receive Buffer - handles partial message reception
// ============================================================================

export class ReceiveBuffer {
 public:
  explicit ReceiveBuffer(std::size_t initial_capacity = 64 * 1024)
      : buffer_(initial_capacity) {}

  // Append received data to buffer
  void Append(std::span<std::byte const> data) {
    EnsureCapacity(write_pos_ + data.size());
    std::copy(data.begin(), data.end(), buffer_.begin() + static_cast<std::ptrdiff_t>(write_pos_));
    write_pos_ += data.size();
  }

  // Get writable region for direct socket read
  [[nodiscard]] std::span<std::byte> GetWriteRegion(std::size_t desired_size) {
    EnsureCapacity(write_pos_ + desired_size);
    return std::span{buffer_.data() + write_pos_, desired_size};
  }

  // Commit bytes written directly to write region
  void CommitWrite(std::size_t bytes_written) {
    write_pos_ += bytes_written;
  }

  // Try to extract a complete message
  [[nodiscard]] std::optional<MessageCodec::DecodeResult> TryExtractMessage() {
    auto const available = AvailableData();
    if (available.empty()) {
      return std::nullopt;
    }

    auto result = MessageCodec::Decode(available);
    if (result) {
      Consume(result->bytes_consumed);
    }
    return result;
  }

  // Try to extract handshake
  [[nodiscard]] std::optional<HandshakeData> TryExtractHandshake() {
    auto const available = AvailableData();
    if (available.size() < kHandshakeSize) {
      return std::nullopt;
    }

    auto result = MessageCodec::DecodeHandshake(available);
    if (result) {
      Consume(kHandshakeSize);
    }
    return result;
  }

  // Check if buffer has enough data for message length field
  [[nodiscard]] std::optional<std::size_t> PeekMessageLength() const {
    return MessageCodec::GetMessageLength(AvailableData());
  }

  // Available data for reading
  [[nodiscard]] std::span<std::byte const> AvailableData() const {
    return std::span{buffer_.data() + read_pos_, write_pos_ - read_pos_};
  }

  [[nodiscard]] std::size_t AvailableBytes() const noexcept {
    return write_pos_ - read_pos_;
  }

  [[nodiscard]] bool Empty() const noexcept { return read_pos_ == write_pos_; }

  void Clear() {
    read_pos_ = 0;
    write_pos_ = 0;
  }

  // Compact buffer by moving unread data to front
  void Compact() {
    if (read_pos_ == 0) return;
    if (read_pos_ == write_pos_) {
      Clear();
      return;
    }

    std::size_t const remaining{write_pos_ - read_pos_};
    std::memmove(buffer_.data(), buffer_.data() + read_pos_, remaining);
    read_pos_ = 0;
    write_pos_ = remaining;
  }

 private:
  void Consume(std::size_t bytes) {
    read_pos_ += bytes;
    // Auto-compact when buffer is mostly consumed
    if (read_pos_ > buffer_.size() / 2) {
      Compact();
    }
  }

  void EnsureCapacity(std::size_t required) {
    if (required <= buffer_.size()) return;

    // Compact first to reclaim space
    Compact();
    if (write_pos_ + (required - read_pos_) <= buffer_.size()) return;

    // Need to grow
    std::size_t new_size{buffer_.size() * 2};
    while (new_size < required) {
      new_size *= 2;
    }
    buffer_.resize(new_size);
  }

  std::vector<std::byte> buffer_{};
  std::size_t read_pos_{0};
  std::size_t write_pos_{0};
};

// ============================================================================
// Send Buffer - queues messages for sending
// ============================================================================

export class SendBuffer {
 public:
  // Queue a message for sending
  void Enqueue(Message const& msg) {
    auto encoded = MessageCodec::Encode(msg);
    Enqueue(std::move(encoded));
  }

  // Queue raw bytes
  void Enqueue(std::vector<std::byte> data) {
    std::lock_guard lock{mutex_};
    pending_.push(std::move(data));
  }

  // Queue handshake
  void EnqueueHandshake(std::span<std::byte const, kInfoHashSize> info_hash,
                        PeerId const& peer_id,
                        ReservedBytes const& reserved = {}) {
    Enqueue(MessageCodec::EncodeHandshake(info_hash, peer_id, reserved));
  }

  // Get next data to send
  [[nodiscard]] std::optional<std::vector<std::byte>> Dequeue() {
    std::lock_guard lock{mutex_};
    if (pending_.empty()) {
      return std::nullopt;
    }
    auto data = std::move(pending_.front());
    pending_.pop();
    return data;
  }

  // Peek at next data without removing
  [[nodiscard]] std::span<std::byte const> Peek() const {
    std::lock_guard lock{mutex_};
    if (pending_.empty()) {
      return {};
    }
    return pending_.front();
  }

  [[nodiscard]] bool Empty() const {
    std::lock_guard lock{mutex_};
    return pending_.empty();
  }

  [[nodiscard]] std::size_t Size() const {
    std::lock_guard lock{mutex_};
    return pending_.size();
  }

  void Clear() {
    std::lock_guard lock{mutex_};
    while (!pending_.empty()) {
      pending_.pop();
    }
  }

 private:
  mutable std::mutex mutex_{};
  std::queue<std::vector<std::byte>> pending_{};
};

// ============================================================================
// Request Queue - tracks pending block requests
// ============================================================================

export class RequestQueue {
 public:
  explicit RequestQueue(std::size_t max_pending = 5)
      : max_pending_{max_pending} {}

  // Add a request
  bool Add(BlockInfo const& block) {
    std::lock_guard lock{mutex_};
    if (requests_.size() >= max_pending_) {
      return false;
    }
    requests_.push_back({block, std::chrono::steady_clock::now()});
    return true;
  }

  // Remove a request (when block received)
  bool Remove(BlockInfo const& block) {
    std::lock_guard lock{mutex_};
    auto it = std::ranges::find_if(
        requests_, [&](auto const& r) { return r.block == block; });
    if (it == requests_.end()) {
      return false;
    }
    requests_.erase(it);
    return true;
  }

  // Check if block is pending
  [[nodiscard]] bool Contains(BlockInfo const& block) const {
    std::lock_guard lock{mutex_};
    return std::ranges::any_of(
        requests_, [&](auto const& r) { return r.block == block; });
  }

  // Get all pending requests
  [[nodiscard]] std::vector<BlockInfo> GetAll() const {
    std::lock_guard lock{mutex_};
    std::vector<BlockInfo> result;
    result.reserve(requests_.size());
    for (auto const& r : requests_) {
      result.push_back(r.block);
    }
    return result;
  }

  // Get timed-out requests
  [[nodiscard]] std::vector<BlockInfo> GetTimedOut(
      std::chrono::seconds timeout) const {
    std::lock_guard lock{mutex_};
    std::vector<BlockInfo> result;
    auto const now = std::chrono::steady_clock::now();

    for (auto const& r : requests_) {
      if (now - r.requested_at > timeout) {
        result.push_back(r.block);
      }
    }
    return result;
  }

  // Clear all requests
  void Clear() {
    std::lock_guard lock{mutex_};
    requests_.clear();
  }

  [[nodiscard]] std::size_t Size() const {
    std::lock_guard lock{mutex_};
    return requests_.size();
  }

  [[nodiscard]] std::size_t Available() const {
    std::lock_guard lock{mutex_};
    return max_pending_ > requests_.size() ? max_pending_ - requests_.size() : 0;
  }

  [[nodiscard]] bool CanRequest() const {
    std::lock_guard lock{mutex_};
    return requests_.size() < max_pending_;
  }

  void SetMaxPending(std::size_t max) {
    std::lock_guard lock{mutex_};
    max_pending_ = max;
  }

 private:
  struct PendingRequest {
    BlockInfo block{};
    std::chrono::steady_clock::time_point requested_at{};
  };

  mutable std::mutex mutex_{};
  std::vector<PendingRequest> requests_{};
  std::size_t max_pending_{};
};

}  // namespace byte_torrent::peer_wire
