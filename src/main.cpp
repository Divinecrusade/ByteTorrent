// ByteTorrent CLI Application
// Downloads files via BitTorrent protocol using byte_torrent components
//
// Usage: byte_torrent <torrent-file> [download-dir]

import <asio.hpp>;

import std;
import torrent;
import torrent_client;
import peer_wire;
import peer_manager;
import tracker;

namespace {

using namespace byte_torrent;

// ============================================================================
// Asio HTTP Client (for tracker communication)
// ============================================================================

class AsioHttpClient final : public tracker::IHttpClient {
 public:
  explicit AsioHttpClient(asio::io_context& io_ctx) : io_ctx_{io_ctx} {}

  [[nodiscard]] tracker::TrackerResult<tracker::HttpResponse> Get(
      tracker::Url const& url,
      std::chrono::seconds /*timeout*/) override {
    try {
      asio::ip::tcp::resolver resolver{io_ctx_};
      asio::ip::tcp::socket socket{io_ctx_};

      auto const endpoints = resolver.resolve(url.host, std::to_string(url.port));
      asio::connect(socket, endpoints);

      // Build and send HTTP request
      std::string const request = tracker::BuildHttpRequest(url);
      asio::write(socket, asio::buffer(request));

      // Set socket options
      socket.set_option(asio::socket_base::receive_buffer_size{65536});

      // Read response
      std::vector<std::byte> response_buffer;
      response_buffer.reserve(8192);

      std::array<std::byte, 4096> chunk{};
      std::error_code ec;

      while (true) {
        auto const bytes_read = socket.read_some(asio::buffer(chunk), ec);
        if (ec == asio::error::eof) {
          break;
        }
        if (ec) {
          return tracker::MakeError<tracker::HttpResponse>(
              tracker::TrackerErrorCode::NetworkError, ec.message());
        }
        response_buffer.insert(response_buffer.end(),
                               chunk.begin(),
                               chunk.begin() + static_cast<std::ptrdiff_t>(bytes_read));
      }

      socket.close();

      // Parse response
      tracker::HttpResponseParser parser;
      parser.Feed(response_buffer);

      if (!parser.IsComplete()) {
        return tracker::MakeError<tracker::HttpResponse>(
            tracker::TrackerErrorCode::InvalidResponse, "Incomplete HTTP response");
      }

      return tracker::MakeSuccess(parser.TakeResponse());

    } catch (std::system_error const& e) {
      return tracker::MakeError<tracker::HttpResponse>(
          tracker::TrackerErrorCode::NetworkError, e.what());
    } catch (std::exception const& e) {
      return tracker::MakeError<tracker::HttpResponse>(
          tracker::TrackerErrorCode::NetworkError, e.what());
    }
  }

 private:
  asio::io_context& io_ctx_;
};

// ============================================================================
// Asio Socket Factory (for peer connections)
// ============================================================================

class AsioSocketFactory final : public ISocketFactory {
 public:
  explicit AsioSocketFactory(asio::io_context& io_ctx) : io_ctx_{io_ctx} {}

  [[nodiscard]] std::unique_ptr<peer_wire::ISocket> CreateSocket() override {
    return peer_wire::CreateAsioSocket(io_ctx_);
  }

  void RunEventLoop() override {
    running_ = true;
    work_guard_.emplace(io_ctx_.get_executor());
    io_ctx_.run();
  }

  void StopEventLoop() override {
    running_ = false;
    work_guard_.reset();
    io_ctx_.stop();
  }

  [[nodiscard]] bool IsRunning() const override { return running_; }

 private:
  asio::io_context& io_ctx_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;
  std::atomic<bool> running_{false};
};

// ============================================================================
// Console Progress Handler
// ============================================================================

class ConsoleProgressHandler final : public ITorrentClientHandler {
 public:
  void OnStateChanged(ClientState new_state) override {
    std::println("[STATE] {}", ToString(new_state));
  }

  void OnProgressUpdated(ClientStats const& stats) override {
    auto const now = std::chrono::steady_clock::now();
    if (now - last_update_ < std::chrono::milliseconds{500}) {
      return;  // Rate-limit console updates
    }
    last_update_ = now;

    auto const formatted = FormatProgress(stats);
    std::println("\r[{}] {} / {} ({}) | D:{} U:{} | Peers: {}/{} | ETA: {}    ",
                 formatted.state,
                 formatted.downloaded,
                 formatted.total_size,
                 formatted.progress_percent,
                 formatted.download_rate,
                 formatted.upload_rate,
                 stats.connected_peers,
                 stats.pending_peers,
                 formatted.eta);
    std::cout << std::flush;
  }

  void OnPieceComplete(std::uint32_t piece_index) override {
    ++pieces_completed_;
    std::println("[PIECE] Completed piece {} (total: {})",
                 piece_index, pieces_completed_);
  }

  void OnDownloadComplete() override {
    std::println("[COMPLETE] Download finished! {} pieces verified.",
                 pieces_completed_);
    download_complete_ = true;
  }

  void OnError(std::string const& message) override {
    std::println(stderr, "[ERROR] {}", message);
    has_error_ = true;
  }

  void OnPeerConnected(peer_wire::PeerEndpoint const& endpoint) override {
    std::println("[PEER+] Connected to {}:{}", endpoint.ip, endpoint.port);
  }

  // NOTE: Requires PATCH 4 to forward error_code from library
  void OnPeerDisconnected(peer_wire::PeerEndpoint const& endpoint,
                          std::error_code ec) override {
    if (ec) {
      std::println("[PEER-] {}:{} - {} ({})", 
                   endpoint.ip, endpoint.port, ec.message(), ec.value());
    } else {
      std::println("[PEER-] {}:{} - closed normally", endpoint.ip, endpoint.port);
    }
  }

  void OnTrackerResponse(std::size_t peer_count) override {
    std::println("[TRACKER] Received {} peers", peer_count);
  }

  void OnTrackerError(std::string const& message) override {
    std::println(stderr, "[TRACKER ERROR] {}", message);
  }

  [[nodiscard]] bool IsComplete() const noexcept { return download_complete_; }
  [[nodiscard]] bool HasError() const noexcept { return has_error_; }

 private:
  std::chrono::steady_clock::time_point last_update_{};
  std::size_t pieces_completed_{0};
  std::atomic<bool> download_complete_{false};
  std::atomic<bool> has_error_{false};
};

// ============================================================================
// Utility Functions
// ============================================================================

void PrintUsage(char const* program_name) {
  std::println("ByteTorrent - BitTorrent Client");
  std::println("Usage: {} <torrent-file> [download-dir]", program_name);
  std::println("");
  std::println("Arguments:");
  std::println("  torrent-file   Path to .torrent file");
  std::println("  download-dir   Download destination (default: current directory)");
  std::println("");
  std::println("Example:");
  std::println("  {} ubuntu.torrent /tmp/downloads", program_name);
}

void PrintTorrentInfo(torrent::Torrent const& t) {
  std::println("Torrent: {}", t.info.name);
  std::println("  Size:        {} ({} pieces x {} bytes)",
               peer_manager::FormatBytes(t.TotalLength()),
               t.PieceCount(),
               t.info.piece_length);
  std::println("  Info Hash:   {}", torrent::Sha1ToHex(t.info_hash));
  std::println("  Tracker:     {}", t.announce);

  if (t.comment) {
    std::println("  Comment:     {}", *t.comment);
  }
  if (t.created_by) {
    std::println("  Created By:  {}", *t.created_by);
  }

  if (t.info.IsMultiFile() && t.info.files) {
    std::println("  Files:       {} files", t.info.files->size());
    for (auto const& f : *t.info.files) {
      std::println("               - {} ({})",
                   f.path.string(),
                   peer_manager::FormatBytes(f.length));
    }
  }
}

// Global flag for signal handling
std::atomic<bool> g_shutdown_requested{false};

}  // namespace

int main(int argc, char* argv[]) {
  // -------------------------------------------------------------------------
  // Argument Parsing
  // -------------------------------------------------------------------------
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::filesystem::path const torrent_path{argv[1]};
  std::filesystem::path download_dir{"."};
  if (argc >= 3) {
    download_dir = argv[2];
  }

  // -------------------------------------------------------------------------
  // Input Validation
  // -------------------------------------------------------------------------
  if (!std::filesystem::exists(torrent_path)) {
    std::println(stderr, "Error: Torrent file not found: {}", torrent_path.string());
    return 1;
  }

  if (!std::filesystem::exists(download_dir)) {
    std::error_code ec;
    if (!std::filesystem::create_directories(download_dir, ec)) {
      std::println(stderr, "Error: Cannot create download directory: {} ({})",
                   download_dir.string(), ec.message());
      return 1;
    }
    std::println("Created download directory: {}", download_dir.string());
  }

  // -------------------------------------------------------------------------
  // Parse Torrent
  // -------------------------------------------------------------------------
  std::println("Loading torrent: {}", torrent_path.string());

  torrent::Torrent torrent_meta;
  try {
    torrent_meta = torrent::ParseFromFile(torrent_path);
  } catch (std::exception const& e) {
    std::println(stderr, "Error: Failed to parse torrent: {}", e.what());
    return 1;
  }

  PrintTorrentInfo(torrent_meta);
  std::println("");

  // -------------------------------------------------------------------------
  // Initialize Asio & Components
  // -------------------------------------------------------------------------
  asio::io_context io_ctx;

  // Configure client
  TorrentClientConfig config{};
  config.download_dir = download_dir;
  config.verify_on_start = false;
  config.max_connections = 50;
  config.enable_endgame = true;

  // Create torrent client
  auto client = std::make_unique<TorrentClient>(std::move(torrent_meta), config);

  // Set up HTTP client for tracker communication
  // NOTE: Requires adding SetHttpClient() to TorrentClient - see patch file
  auto http_client = std::make_unique<AsioHttpClient>(io_ctx);
  client->SetHttpClient(std::move(http_client));

  // Set up socket factory for peer connections
  AsioSocketFactory socket_factory{io_ctx};
  client->SetSocketFactory(&socket_factory);

  // Set up progress handler
  ConsoleProgressHandler handler;
  client->SetHandler(&handler);

  // -------------------------------------------------------------------------
  // Start Download
  // -------------------------------------------------------------------------
  std::println("Starting download... (Press Ctrl+C to stop)");
  client->Start();

  // Single-threaded event loop to avoid race conditions in peer_manager
  // Note: After patching peer_manager.ixx, we must call ProcessPendingRemovals()
  // to safely delete disconnected peers outside of their callbacks.
  auto last_update = std::chrono::steady_clock::now();
  constexpr auto kUpdateInterval = std::chrono::milliseconds{100};

  try {
    while (!handler.IsComplete() && !handler.HasError() && !g_shutdown_requested) {
      // Process Asio events (non-blocking)
      io_ctx.poll();

      // Periodically call Update() on the same thread
      auto const now = std::chrono::steady_clock::now();
      if (now - last_update >= kUpdateInterval) {
        client->Update();
        last_update = now;
      }

      // Small sleep to avoid busy-waiting
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  } catch (std::exception const& e) {
    std::println(stderr, "Event loop error: {}", e.what());
  }

  // -------------------------------------------------------------------------
  // Cleanup
  // -------------------------------------------------------------------------
  client->Stop();

  if (handler.IsComplete()) {
    std::println("Download completed successfully!");
    std::println("Files saved to: {}", download_dir.string());
    return 0;
  }

  if (handler.HasError()) {
    std::println(stderr, "Download failed due to errors.");
    return 1;
  }

  std::println("Download interrupted.");
  return 130;
}
