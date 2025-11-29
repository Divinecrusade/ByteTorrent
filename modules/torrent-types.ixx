export module torrent:types;

import std;

namespace byte_torrent::torrent {
// SHA1 hash is always 20 bytes
constexpr std::size_t kSha1HashSize = 20u;
export using Sha1Hash = std::array<std::byte, kSha1HashSize>;

export struct FileInfo {
  std::filesystem::path path{};
  std::uint64_t length{};
  std::optional<std::string> md5sum{std::nullopt};  // rarely used, but part of spec

  [[nodiscard]] friend constexpr bool operator==(FileInfo const&,
                                                 FileInfo const&) = default;
};

export struct Info {
  std::string name{};   
  std::uint64_t piece_length{};
  std::vector<Sha1Hash> pieces{};

  // Single-file mode: only length is present
  std::optional<std::uint64_t> length{std::nullopt};

  // Multi-file mode: files list is present
  std::optional<std::vector<FileInfo>> files{std::nullopt};

  // Optional fields
  std::optional<bool> is_private{std::nullopt};  // private tracker flag

  [[nodiscard]] constexpr bool IsMultiFile() const noexcept {
    return files.has_value();
  }

  [[nodiscard]] constexpr bool IsSingleFile() const noexcept {
    return length.has_value();
  }

  [[nodiscard]] std::uint64_t TotalLength() const noexcept {
    if (length.has_value()) {
      return *length;
    }
    if (!files.has_value()) {
      return 0;
    }
    return std::accumulate(
        files->begin(), files->end(), std::uint64_t{0},
        [](std::uint64_t acc, FileInfo const& f) { return acc + f.length; });
  }

  [[nodiscard]] std::size_t PieceCount() const noexcept {
    return pieces.size();
  }

  [[nodiscard]] friend constexpr bool operator==(Info const&,
                                                 Info const&) = default;
};

// Complete .torrent file representation
export struct Torrent {
  std::string announce{};  // Primary tracker URL
  Info info{};
  Sha1Hash info_hash{};    // SHA1 of bencoded info dict (for peer protocol)

  // BEP-0012: Multi-tracker metadata extension
  std::optional<std::vector<std::vector<std::string>>> announce_list{};

  // Optional metadata
  std::optional<std::chrono::system_clock::time_point> creation_date{};
  std::optional<std::string> comment{};
  std::optional<std::string> created_by{};
  std::optional<std::string> encoding{};  // String encoding hint

  [[nodiscard]] std::size_t PieceCount() const noexcept {
    return info.PieceCount();
  }

  [[nodiscard]] std::uint64_t TotalLength() const noexcept {
    return info.TotalLength();
  }

  [[nodiscard]] bool IsMultiFile() const noexcept { return info.IsMultiFile(); }

  [[nodiscard]] friend constexpr bool operator==(Torrent const&,
                                                 Torrent const&) = default;
};

namespace validation {
export [[nodiscard]] constexpr bool IsValidPieceLength(
    std::uint64_t piece_length) noexcept {
  // Must be power of 2, typically 16KB - 16MB
  return piece_length > 0 && (piece_length & (piece_length - 1)) == 0;
}

export [[nodiscard]] inline bool IsValidPiecesData(
    std::span<std::byte const> raw_pieces) noexcept {
  return !raw_pieces.empty() && (raw_pieces.size() % kSha1HashSize == 0);
}

export [[nodiscard]] inline std::size_t ExpectedPieceCount(
    std::uint64_t total_length, std::uint64_t piece_length) noexcept {
  if (piece_length == 0) return 0;
  return static_cast<std::size_t>((total_length + piece_length - 1) /
                                  piece_length);
}
}  // namespace byte_torrent::torrent::validation
}  // namespace byte_torrent::torrent
