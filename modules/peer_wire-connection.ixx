export module peer_wire:connection;

import std;
import :types;
import :message;
import :buffer;

namespace byte_torrent::peer_wire {

// ============================================================================
// Connection Handler Interface (callbacks)
// ============================================================================

export class IConnectionHandler {
 public:
  virtual ~IConnectionHandler() = default;

  // Connection lifecycle
  virtual void OnConnected() = 0;
  virtual void OnHandshakeComplete(HandshakeData const& handshake) = 0;
  virtual void OnDisconnected(std::error_code ec) = 0;
  virtual void OnError(std::error_code ec) = 0;

  // Message events
  virtual void OnMessage(Message const& msg) = 0;

  // Specific message handlers (optional, can use OnMessage instead)
  virtual void OnChoke() {}
  virtual void OnUnchoke() {}
  virtual void OnInterested() {}
  virtual void OnNotInterested() {}
  virtual void OnHave(std::uint32_t piece_index) { std::ignore = piece_index; }
  virtual void OnBitfield(Bitfield const& bitfield) { std::ignore = bitfield; }
  virtual void OnRequest(BlockInfo const& block) { std::ignore = block; }
  virtual void OnPiece(std::uint32_t piece_index, std::uint32_t offset,
                       std::span<std::byte const> data) {
    std::ignore = piece_index;
    std::ignore = offset;
    std::ignore = data;
  }
  virtual void OnCancel(BlockInfo const& block) { std::ignore = block; }
  virtual void OnPort(std::uint16_t port) { std::ignore = port; }
};

// ============================================================================
// Null Handler (for testing)
// ============================================================================

export class NullConnectionHandler : public IConnectionHandler {
 public:
  void OnConnected() override {}
  void OnHandshakeComplete(HandshakeData const&) override {}
  void OnDisconnected(std::error_code) override {}
  void OnError(std::error_code) override {}
  void OnMessage(Message const&) override {}
};

// ============================================================================
// Connection Configuration
// ============================================================================

export struct ConnectionConfig {
  std::chrono::seconds connect_timeout{30};
  std::chrono::seconds handshake_timeout{30};
  std::chrono::seconds request_timeout{60};
  std::chrono::seconds keep_alive_interval{120};
  std::size_t max_pending_requests{5};
  std::size_t receive_buffer_size{64 * 1024};
  bool enable_fast_extension{false};   // BEP-6
  bool enable_extension_protocol{false}; // BEP-10
};

// ============================================================================
// Socket Interface (for dependency injection/testing)
// ============================================================================

export class ISocket {
 public:
  virtual ~ISocket() = default;

  virtual void Connect(PeerEndpoint const& endpoint,
                       std::function<void(std::error_code)> callback) = 0;
  virtual void Close() = 0;
  virtual bool IsOpen() const = 0;

  virtual void AsyncRead(std::span<std::byte> buffer,
                         std::function<void(std::error_code, std::size_t)> callback) = 0;
  virtual void AsyncWrite(std::span<std::byte const> data,
                          std::function<void(std::error_code, std::size_t)> callback) = 0;

  virtual void SetReadTimeout(std::chrono::seconds timeout) = 0;
  virtual void SetWriteTimeout(std::chrono::seconds timeout) = 0;

  virtual PeerEndpoint GetRemoteEndpoint() const = 0;
  virtual PeerEndpoint GetLocalEndpoint() const = 0;
};

// ============================================================================
// Peer Connection
// ============================================================================

export class PeerConnection : public std::enable_shared_from_this<PeerConnection> {
 public:
  PeerConnection(std::unique_ptr<ISocket> socket,
                 std::array<std::byte, kInfoHashSize> info_hash,
                 PeerId local_peer_id,
                 std::size_t piece_count,
                 ConnectionConfig config = {})
      : socket_{std::move(socket)},
        info_hash_{info_hash},
        local_peer_id_{local_peer_id},
        piece_count_{piece_count},
        config_{std::move(config)},
        receive_buffer_{config_.receive_buffer_size},
        request_queue_{config_.max_pending_requests},
        remote_bitfield_{piece_count} {}

  ~PeerConnection() { Close(); }

  // Non-copyable, movable
  PeerConnection(PeerConnection const&) = delete;
  PeerConnection& operator=(PeerConnection const&) = delete;
  PeerConnection(PeerConnection&&) noexcept = default;
  PeerConnection& operator=(PeerConnection&&) noexcept = default;

  // -------------------------------------------------------------------------
  // Connection Lifecycle
  // -------------------------------------------------------------------------

  // Connect to peer (outbound connection)
  void Connect(PeerEndpoint const& endpoint) {
    if (state_ != ConnectionState::Disconnected) {
      return;
    }

    endpoint_ = endpoint;
    SetState(ConnectionState::Connecting);

    socket_->Connect(endpoint, [this, self = shared_from_this()](std::error_code ec) {
      if (ec) {
        HandleError(ec);
        return;
      }
      OnSocketConnected();
    });
  }

  // Accept connection (inbound connection, socket already connected)
  void Accept() {
    if (state_ != ConnectionState::Disconnected || !socket_->IsOpen()) {
      return;
    }

    endpoint_ = socket_->GetRemoteEndpoint();
    SetState(ConnectionState::Handshaking);
    StartRead();
    // Wait for peer's handshake first before sending ours
  }

  // Close connection
  void Close() {
    if (state_ == ConnectionState::Disconnected) {
      return;
    }

    SetState(ConnectionState::Closing);
    socket_->Close();
    request_queue_.Clear();
    send_buffer_.Clear();
    SetState(ConnectionState::Disconnected);

    if (handler_) {
      handler_->OnDisconnected({});
    }
  }

  // -------------------------------------------------------------------------
  // Message Sending
  // -------------------------------------------------------------------------

  void SendChoke() {
    if (!CanSend()) return;
    peer_state_.am_choking = true;
    QueueMessage(Choke{});
  }

  void SendUnchoke() {
    if (!CanSend()) return;
    peer_state_.am_choking = false;
    QueueMessage(Unchoke{});
  }

  void SendInterested() {
    if (!CanSend()) return;
    peer_state_.am_interested = true;
    QueueMessage(Interested{});
  }

  void SendNotInterested() {
    if (!CanSend()) return;
    peer_state_.am_interested = false;
    QueueMessage(NotInterested{});
  }

  void SendHave(std::uint32_t piece_index) {
    if (!CanSend()) return;
    QueueMessage(Have{.piece_index = piece_index});
  }

  void SendBitfield(Bitfield const& bitfield) {
    if (!CanSend()) return;
    QueueMessage(bitfield);
  }

  void SendRequest(BlockInfo const& block) {
    if (!CanSend()) return;
    if (!request_queue_.Add(block)) {
      return; // Queue full
    }
    QueueMessage(Request{.block = block});
  }

  void SendPiece(std::uint32_t piece_index, std::uint32_t offset,
                 std::span<std::byte const> data) {
    if (!CanSend()) return;
    Piece piece{.piece_index = piece_index, .offset = offset};
    piece.data.assign(data.begin(), data.end());
    QueueMessage(piece);
    stats_.bytes_uploaded += data.size();
  }

  void SendCancel(BlockInfo const& block) {
    if (!CanSend()) return;
    request_queue_.Remove(block);
    QueueMessage(Cancel{.block = block});
  }

  void SendKeepAlive() {
    if (!CanSend()) return;
    QueueMessage(KeepAlive{});
  }

  // -------------------------------------------------------------------------
  // State Queries
  // -------------------------------------------------------------------------

  [[nodiscard]] ConnectionState GetState() const noexcept { return state_; }
  [[nodiscard]] PeerState const& GetPeerState() const noexcept { return peer_state_; }
  [[nodiscard]] PeerEndpoint const& GetEndpoint() const noexcept { return endpoint_; }
  [[nodiscard]] std::optional<PeerId> GetRemotePeerId() const noexcept {
    if (remote_peer_id_) return *remote_peer_id_;
    return std::nullopt;
  }
  [[nodiscard]] Bitfield const& GetRemoteBitfield() const noexcept {
    return remote_bitfield_;
  }
  [[nodiscard]] PeerStats const& GetStats() const noexcept { return stats_; }

  [[nodiscard]] bool HasPiece(std::size_t index) const noexcept {
    return remote_bitfield_.HasPiece(index);
  }

  [[nodiscard]] std::size_t PendingRequestCount() const noexcept {
    return request_queue_.Size();
  }

  [[nodiscard]] bool CanRequest() const noexcept {
    return state_ == ConnectionState::Connected &&
           !peer_state_.peer_choking &&
           peer_state_.am_interested &&
           request_queue_.CanRequest();
  }

  [[nodiscard]] std::size_t AvailableRequestSlots() const noexcept {
    if (!CanRequest()) return 0;
    return request_queue_.Available();
  }

  [[nodiscard]] std::vector<BlockInfo> GetPendingRequests() const {
    return request_queue_.GetAll();
  }

  [[nodiscard]] bool IsConnected() const noexcept {
    return state_ == ConnectionState::Connected;
  }

  // -------------------------------------------------------------------------
  // Handler Management
  // -------------------------------------------------------------------------

  void SetHandler(IConnectionHandler* handler) { handler_ = handler; }

  // -------------------------------------------------------------------------
  // Configuration
  // -------------------------------------------------------------------------

  void SetMaxPendingRequests(std::size_t max) {
    request_queue_.SetMaxPending(max);
  }

 private:
  // -------------------------------------------------------------------------
  // Internal State Management
  // -------------------------------------------------------------------------

  void SetState(ConnectionState new_state) {
    state_ = new_state;
  }

  [[nodiscard]] bool CanSend() const noexcept {
    return state_ == ConnectionState::Connected ||
           state_ == ConnectionState::Handshaking;
  }

  // -------------------------------------------------------------------------
  // Connection Handling
  // -------------------------------------------------------------------------

  void OnSocketConnected() {
    SetState(ConnectionState::Handshaking);
    stats_.connected_at = std::chrono::steady_clock::now();

    // Send our handshake
    SendHandshake();
    StartRead();
  }

  void SendHandshake() {
    ReservedBytes reserved{};
    if (config_.enable_extension_protocol) {
      reserved = extensions::WithExtensionProtocol();
    }
    send_buffer_.EnqueueHandshake(info_hash_, local_peer_id_, reserved);
    FlushSendBuffer();
  }

  void StartRead() {
    if (!socket_->IsOpen()) return;

    // Request buffer large enough for max block + message overhead
    // Max block is 32KB, plus ~13 bytes header = ~33KB
    constexpr std::size_t kReadSize = 64 * 1024;
    auto write_region = receive_buffer_.GetWriteRegion(kReadSize);
    socket_->AsyncRead(
        write_region,
        [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
          OnReadComplete(ec, bytes);
        });
  }

  void OnReadComplete(std::error_code ec, std::size_t bytes_read) {
    if (ec) {
      HandleError(ec);
      return;
    }

    if (bytes_read == 0) {
      // Connection closed
      HandleError(std::make_error_code(std::errc::connection_reset));
      return;
    }

    receive_buffer_.CommitWrite(bytes_read);
    stats_.last_received_at = std::chrono::steady_clock::now();

    ProcessReceivedData();

    // Continue reading
    if (state_ != ConnectionState::Disconnected &&
        state_ != ConnectionState::Error) {
      StartRead();
    }
  }

  void ProcessReceivedData() {
    // Handle handshake first
    if (state_ == ConnectionState::Handshaking && !handshake_complete_) {
      if (auto handshake = receive_buffer_.TryExtractHandshake()) {
        OnHandshakeReceived(*handshake);
      }
      return;
    }

    // Process messages
    while (auto result = receive_buffer_.TryExtractMessage()) {
      OnMessageReceived(result->message);
    }
  }

  void OnHandshakeReceived(HandshakeData const& handshake) {
    // Verify info_hash matches
    if (handshake.info_hash != info_hash_) {
      HandleError(std::make_error_code(std::errc::protocol_error));
      return;
    }

    remote_peer_id_ = handshake.peer_id;
    remote_reserved_ = handshake.reserved;
    handshake_complete_ = true;

    // If this was inbound connection, send our handshake now
    if (send_buffer_.Empty()) {
      SendHandshake();
    }

    SetState(ConnectionState::Connected);

    if (handler_) {
      handler_->OnConnected();
      handler_->OnHandshakeComplete(handshake);
    }
  }

  void OnMessageReceived(Message const& msg) {
    ++stats_.messages_received;

    // Dispatch to specific handlers
    std::visit([this](auto const& m) { HandleMessage(m); }, msg);

    // Generic handler
    if (handler_) {
      handler_->OnMessage(msg);
    }
  }

  // -------------------------------------------------------------------------
  // Message Handlers
  // -------------------------------------------------------------------------

  void HandleMessage(KeepAlive const&) {
    // Just update last_received timestamp (already done)
  }

  void HandleMessage(Choke const&) {
    peer_state_.peer_choking = true;
    // Cancel pending requests on choke
    auto pending = request_queue_.GetAll();
    request_queue_.Clear();
    if (handler_) {
      handler_->OnChoke();
    }
  }

  void HandleMessage(Unchoke const&) {
    peer_state_.peer_choking = false;
    if (handler_) {
      handler_->OnUnchoke();
    }
  }

  void HandleMessage(Interested const&) {
    peer_state_.peer_interested = true;
    if (handler_) {
      handler_->OnInterested();
    }
  }

  void HandleMessage(NotInterested const&) {
    peer_state_.peer_interested = false;
    if (handler_) {
      handler_->OnNotInterested();
    }
  }

  void HandleMessage(Have const& msg) {
    if (msg.piece_index < piece_count_) {
      remote_bitfield_.SetPiece(msg.piece_index);
    }
    if (handler_) {
      handler_->OnHave(msg.piece_index);
    }
  }

  void HandleMessage(Bitfield const& msg) {
    remote_bitfield_ = msg;
    if (handler_) {
      handler_->OnBitfield(msg);
    }
  }

  void HandleMessage(Request const& msg) {
    if (handler_) {
      handler_->OnRequest(msg.block);
    }
  }

  void HandleMessage(Piece const& msg) {
    BlockInfo const block{
        .piece_index = msg.piece_index,
        .offset = msg.offset,
        .length = static_cast<std::uint32_t>(msg.data.size())};

    request_queue_.Remove(block);
    stats_.bytes_downloaded += msg.data.size();

    if (handler_) {
      handler_->OnPiece(msg.piece_index, msg.offset,
                        std::span{msg.data.data(), msg.data.size()});
    }
  }

  void HandleMessage(Cancel const& msg) {
    if (handler_) {
      handler_->OnCancel(msg.block);
    }
  }

  void HandleMessage(Port const& msg) {
    if (handler_) {
      handler_->OnPort(msg.listen_port);
    }
  }

  // -------------------------------------------------------------------------
  // Send Handling
  // -------------------------------------------------------------------------

  void QueueMessage(Message const& msg) {
    send_buffer_.Enqueue(msg);
    ++stats_.messages_sent;
    FlushSendBuffer();
  }

  void FlushSendBuffer() {
    if (is_sending_ || send_buffer_.Empty()) {
      return;
    }

    auto data = send_buffer_.Dequeue();
    if (!data) return;

    is_sending_ = true;
    current_send_data_ = std::move(*data);

    socket_->AsyncWrite(
        current_send_data_,
        [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
          OnWriteComplete(ec, bytes);
        });
  }

  void OnWriteComplete(std::error_code ec, std::size_t bytes_written) {
    std::ignore = bytes_written;
    is_sending_ = false;

    if (ec) {
      HandleError(ec);
      return;
    }

    stats_.last_sent_at = std::chrono::steady_clock::now();

    // Send next message if any
    FlushSendBuffer();
  }

  // -------------------------------------------------------------------------
  // Error Handling
  // -------------------------------------------------------------------------

  void HandleError(std::error_code ec) {
    if (state_ == ConnectionState::Error ||
        state_ == ConnectionState::Disconnected) {
      return;
    }

    SetState(ConnectionState::Error);
    socket_->Close();

    if (handler_) {
      handler_->OnError(ec);
      handler_->OnDisconnected(ec);
    }
  }

  // -------------------------------------------------------------------------
  // Member Variables
  // -------------------------------------------------------------------------

  std::unique_ptr<ISocket> socket_{};
  std::array<std::byte, kInfoHashSize> info_hash_{};
  PeerId local_peer_id_{};
  std::size_t piece_count_{};
  ConnectionConfig config_{};

  ConnectionState state_{ConnectionState::Disconnected};
  PeerEndpoint endpoint_{};
  PeerState peer_state_{};
  PeerStats stats_{};

  ReceiveBuffer receive_buffer_;
  SendBuffer send_buffer_{};
  RequestQueue request_queue_;

  Bitfield remote_bitfield_{};
  std::optional<PeerId> remote_peer_id_{};
  ReservedBytes remote_reserved_{};
  bool handshake_complete_{false};

  bool is_sending_{false};
  std::vector<std::byte> current_send_data_{};

  IConnectionHandler* handler_{nullptr};
};

// ============================================================================
// Factory Function
// ============================================================================

export [[nodiscard]] std::shared_ptr<PeerConnection> CreateConnection(
    std::unique_ptr<ISocket> socket,
    std::array<std::byte, kInfoHashSize> info_hash,
    PeerId local_peer_id,
    std::size_t piece_count,
    ConnectionConfig config = {}) {
  return std::make_shared<PeerConnection>(
      std::move(socket), info_hash, local_peer_id, piece_count, std::move(config));
}

}  // namespace byte_torrent::peer_wire
