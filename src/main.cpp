#define ASIO_STANDALONE
#include <asio.hpp>

import std;
import torrent_client;
import torrent;
import peer_wire;

using namespace byte_torrent;

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

  // TODO:
  // using byte_torrent components and ASIO
  // download file via torrent

  return 0;
}
