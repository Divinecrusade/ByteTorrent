// ByteTorrent - Example Usage
// This file demonstrates how to use the TorrentClient

import std;
import torrent_client;
import torrent;
import peer_wire;

using namespace byte_torrent;

// Custom event handler for progress reporting
class MyHandler : public ITorrentClientHandler {
 public:
  void OnStateChanged(ClientState new_state) override {
    std::println("State changed: {}", ToString(new_state));
  }

  void OnProgressUpdated(ClientStats const& stats) override {
    // Print progress every few updates
    if (++update_count_ % 10 == 0) {
      PrintProgress(stats);
    }
  }

  void OnPieceComplete(std::uint32_t piece_index) override {
    std::println("Piece {} complete", piece_index);
  }

  void OnDownloadComplete() override {
    std::println("\n*** Download complete! ***\n");
  }

  void OnError(std::string const& message) override {
    std::println("Error: {}", message);
  }

  void OnPeerConnected(peer_wire::PeerEndpoint const& endpoint) override {
    std::println("Peer connected: {}", endpoint.ToString());
  }

  void OnPeerDisconnected(peer_wire::PeerEndpoint const& endpoint) override {
    std::println("Peer disconnected: {}", endpoint.ToString());
  }

  void OnTrackerResponse(std::size_t peer_count) override {
    std::println("Tracker responded with {} new peers", peer_count);
  }

  void OnTrackerError(std::string const& message) override {
    std::println("Tracker error: {}", message);
  }

 private:
  std::size_t update_count_{0};
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::println("Usage: {} <torrent-file> [download-dir]", argv[0]);
    std::println("\nExample:");
    std::println("  {} ubuntu.torrent /tmp/downloads", argv[0]);
    return 1;
  }

  std::filesystem::path const torrent_path{argv[1]};
  std::filesystem::path download_dir{"."};
  if (argc >= 3) {
    download_dir = argv[2];
  }

  // Check if torrent file exists
  if (!std::filesystem::exists(torrent_path)) {
    std::println("Error: Torrent file not found: {}", torrent_path.string());
    return 1;
  }

  try {
    // Configure the client
    TorrentClientConfig config{};
    config.listen_port = 6881;
    config.max_connections = 50;
    config.max_upload_slots = 4;
    config.download_dir = download_dir;
    config.verify_on_start = true;
    config.seed_after_complete = true;

    // Create client from torrent file
    auto client = CreateClientFromFile(torrent_path, config);

    // Print torrent info
    auto const& torrent = client->GetTorrent();
    std::println("Torrent: {}", torrent.info.name);
    std::println("Size: {}", peer_manager::FormatBytes(torrent.TotalLength()));
    std::println("Pieces: {} x {}",
                 torrent.PieceCount(),
                 peer_manager::FormatBytes(torrent.info.piece_length));
    std::println("Tracker: {}", torrent.announce);
    std::println("");

    // Set up event handler
    MyHandler handler{};
    client->SetHandler(&handler);

    // Enable streaming mode (prioritize first/last pieces)
    // client->EnableStreamingMode();

    // Start the download
    std::println("Starting download...\n");
    client->Start();

    // Main loop
    // In a real application, you would integrate this with your event loop
    // or run it in a separate thread
    while (client->IsRunning()) {
      client->Update();

      // Check for user interrupt (Ctrl+C)
      // In real code, you'd set up a signal handler

      // Small sleep to avoid busy-waiting
      std::this_thread::sleep_for(std::chrono::milliseconds{100});

      // For demo: stop after 30 seconds or when complete
      auto stats = client->GetStats();
      auto elapsed = std::chrono::steady_clock::now() - stats.started_at;
      if (elapsed > std::chrono::seconds{30} || stats.state == ClientState::Seeding) {
        break;
      }
    }

    // Print final stats
    auto final_stats = client->GetStats();
    std::println("\nFinal Statistics:");
    std::println("  Downloaded: {}", peer_manager::FormatBytes(final_stats.downloaded_bytes));
    std::println("  Uploaded: {}", peer_manager::FormatBytes(final_stats.uploaded_bytes));
    std::println("  Progress: {:.1f}%", final_stats.progress * 100.0);
    std::println("  Peers: {}", final_stats.connected_peers);

    // Stop the client
    client->Stop();
    std::println("\nClient stopped.");

  } catch (std::exception const& e) {
    std::println("Fatal error: {}", e.what());
    return 1;
  }

  return 0;
}
