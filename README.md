# ByteTorrent

A BitTorrent client implementation in C++23 using modules.

## Features

- **Bencode**: Full encoder/decoder for BitTorrent's data format
- **Torrent Parsing**: Parse .torrent files with complete metadata extraction
- **Peer Wire Protocol**: Complete implementation of the BitTorrent peer protocol
- **HTTP Tracker**: Client for tracker announce/scrape operations
- **Piece Manager**: Rarest-first piece selection, SHA1 verification, endgame mode
- **Peer Manager**: Connection management, choking algorithm, peer statistics
- **Torrent Client**: High-level orchestration with event-driven architecture

## **Supported (.torrent v1)**

| Feature | Status | Notes |
| ----- | ----- | ----- |
| **Single-file torrents** | ✅ | `info.length` field |
| **Multi-file torrents** | ✅ | `info.files` with nested paths |
| **Multi-tracker** | ✅ | BEP-12 `announce-list` |
| **Private flag** | ✅ | `info.private` |
| **MD5 checksums** | ✅ | Per-file `md5sum` (rarely used) |
| **Metadata fields** | ✅ | `comment`, `created by`, `creation date`, `encoding` |
| **Compact peer lists** | ✅ | BEP-23 (6 bytes per peer) |
| **IPv6 peers** | ✅ | `peers6` field (18 bytes per peer) |

## **NOT Supported**

| Feature | BEP | Notes |
| ----- | ----- | ----- |
| **v2 torrents** | BEP-52 | Uses SHA256, merkle trees, per-file piece alignment |
| **Hybrid v1+v2** | BEP-52 | Combined format |
| **Magnet links** | BEP-9 | Metadata exchange protocol needed |
| **DHT** | BEP-5 | Distributed hash table |
| **PEX** | BEP-11 | Peer exchange |
| **Web seeds** | BEP-19 | HTTP/FTP sources |
| **Padding files** | BEP-47 | For piece alignment |

Support of v2 torrent files would require significant changes:
- New hash type: SHA256 (32 bytes) instead of SHA1 (20 bytes)
- File tree structure: Replace flat files list with nested dictionary
- Per-file pieces: Each file has its own piece boundaries
- Merkle trees: Piece layers with root hashes
- Padding: Files padded to piece boundaries

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     TorrentClient                           │
│              (High-level orchestration)                     │
└──────┬───────────┬───────────┬───────────┬──────────────────┘
       │           │           │           │
┌──────▼───┐ ┌─────▼────┐ ┌────▼────┐ ┌────▼─────┐
│  Peer    │ │  Piece   │ │ Tracker │ │ Torrent  │
│ Manager  │ │ Manager  │ │ Client  │ │ Parser   │
└────┬─────┘ └────┬─────┘ └─────────┘ └──────────┘
     │            │
┌────▼────────────▼────┐
│     Peer Wire        │
│ (Protocol Messages)  │
└──────────────────────┘
         │
    ┌────▼────┐
    │ Bencode │
    └─────────┘
```

## Modules

| Module | Description |
|--------|-------------|
| `bencode` | Encode/decode bencode format (integers, strings, lists, dictionaries) |
| `torrent` | Parse .torrent files, extract metadata, compute info_hash |
| `peer_wire` | Protocol messages, handshake, connection state machine |
| `tracker` | HTTP tracker announce/scrape, URL encoding, response parsing |
| `piece_manager` | Piece selection (rarest-first), block tracking, SHA1 verification |
| `peer_manager` | Multiple peer connections, choking algorithm, statistics |
| `torrent_client` | High-level API, event loop, periodic tasks |

## Building

### Prerequisites

- CMake 3.28+
- Visual Studio 2022 (MSVC) with C++23 modules support
- vcpkg with packages: `gtest`, `openssl`
- Optional: `asio` for real network connections

### Build Commands

```bash
# Configure with vcpkg
cmake --preset vs-debug

# Build
cmake --build build/debug

# Run tests
ctest --test-dir build/debug --output-on-failure
```

## Wire Protocol Messages

| Message | ID | Description |
|---------|---:|-------------|
| KeepAlive | - | Empty message, keep connection alive |
| Choke | 0 | Stop sending pieces to peer |
| Unchoke | 1 | Allow peer to request pieces |
| Interested | 2 | Peer wants pieces we have |
| NotInterested | 3 | Peer doesn't need our pieces |
| Have | 4 | Announce we have a piece |
| Bitfield | 5 | Send complete piece availability |
| Request | 6 | Request a block (piece_index, offset, length) |
| Piece | 7 | Block data response |
| Cancel | 8 | Cancel a pending request |
| Port | 9 | DHT port (BEP-5) |

## Testing

The project includes comprehensive unit tests (~400 tests):

```bash
# Run all tests
ctest --test-dir build/debug

# Run specific test suite
./build/debug/tests/bencode_test
./build/debug/tests/torrent_test
./build/debug/tests/peer_wire_test
./build/debug/tests/tracker_test
./build/debug/tests/piece_manager_test
./build/debug/tests/peer_manager_test
./build/debug/tests/torrent_client_test
```

## Future Enhancements

- [ ] DHT (BEP-5) - Distributed Hash Table
- [ ] PEX (BEP-11) - Peer Exchange
- [ ] Magnet links (BEP-9)
- [ ] Fast Extension (BEP-6)
- [ ] Extension Protocol (BEP-10)
- [ ] UDP tracker support (BEP-15)
- [ ] µTP (BEP-29) - Micro Transport Protocol
- [ ] Encryption (BEP-??) - Message Stream Encryption
- [ ] Disk I/O with async file operations
- [ ] Web seeds (BEP-19)

## License

MIT License - See [LICENSE.txt](LICENSE.txt)

## References

- [BEP-3: The BitTorrent Protocol Specification](https://www.bittorrent.org/beps/bep_0003.html)
- [BEP-12: Multitracker Metadata Extension](https://www.bittorrent.org/beps/bep_0012.html)
- [BEP-23: Tracker Returns Compact Peer Lists](https://www.bittorrent.org/beps/bep_0023.html)
- [Unofficial BitTorrent Specification](https://wiki.theory.org/BitTorrentSpecification)
