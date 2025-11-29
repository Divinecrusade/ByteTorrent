#include <gtest/gtest.h>
import torrent;
#include <chrono>
#include <vector>

using namespace byte_torrent::torrent;
using namespace std::chrono_literals;

// ============================================================================
// Test Fixture
// ============================================================================

class TorrentTypesTest : public ::testing::Test {
 protected:
  static Sha1Hash MakeDummyHash(std::byte fill = std::byte{0xAB}) {
    Sha1Hash hash{};
    std::ranges::fill(hash, fill);
    return hash;
  }

  static FileInfo MakeFileInfo(std::string_view path, std::uint64_t length) {
    return FileInfo{.path = std::filesystem::path{path}, .length = length};
  }
};

// ============================================================================
// FileInfo Tests
// ============================================================================

TEST_F(TorrentTypesTest, FileInfoDefaultConstruction) {
  FileInfo const info{};

  EXPECT_TRUE(info.path.empty());
  EXPECT_EQ(info.length, 0);
  EXPECT_FALSE(info.md5sum.has_value());
}

TEST_F(TorrentTypesTest, FileInfoWithMd5sum) {
  FileInfo const info{.path = "test.txt",
                      .length = 1024,
                      .md5sum = "d41d8cd98f00b204e9800998ecf8427e"};

  EXPECT_EQ(info.path, "test.txt");
  EXPECT_EQ(info.length, 1024);
  ASSERT_TRUE(info.md5sum.has_value());
  EXPECT_EQ(info.md5sum->size(), 32);
}

TEST_F(TorrentTypesTest, FileInfoEquality) {
  FileInfo const a{.path = "file.bin", .length = 512};
  FileInfo const b{.path = "file.bin", .length = 512};
  FileInfo const c{.path = "other.bin", .length = 512};

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

// ============================================================================
// Info Tests - Single File Mode
// ============================================================================

TEST_F(TorrentTypesTest, InfoSingleFileMode) {
  Info const info{.name = "movie.mkv",
                  .piece_length = 262144,
                  .pieces = {MakeDummyHash()},
                  .length = 1048576};

  EXPECT_TRUE(info.IsSingleFile());
  EXPECT_FALSE(info.IsMultiFile());
  EXPECT_EQ(info.TotalLength(), 1048576);
  EXPECT_EQ(info.PieceCount(), 1);
}

TEST_F(TorrentTypesTest, InfoSingleFileTotalLength) {
  Info const info{.name = "test.bin",
                  .piece_length = 16384,
                  .pieces = {},
                  .length = 999999};

  EXPECT_EQ(info.TotalLength(), 999999);
}

// ============================================================================
// Info Tests - Multi File Mode
// ============================================================================

TEST_F(TorrentTypesTest, InfoMultiFileMode) {
  std::vector<FileInfo> files{
      MakeFileInfo("dir/file1.txt", 100),
      MakeFileInfo("dir/file2.txt", 200),
      MakeFileInfo("dir/subdir/file3.txt", 300),
  };

  Info const info{.name = "my_torrent",
                  .piece_length = 16384,
                  .pieces = {MakeDummyHash(), MakeDummyHash()},
                  .files = std::move(files)};

  EXPECT_FALSE(info.IsSingleFile());
  EXPECT_TRUE(info.IsMultiFile());
  EXPECT_EQ(info.TotalLength(), 600);
  EXPECT_EQ(info.PieceCount(), 2);
}

TEST_F(TorrentTypesTest, InfoMultiFileEmptyFiles) {
  Info const info{.name = "empty_torrent",
                  .piece_length = 16384,
                  .pieces = {},
                  .files = std::vector<FileInfo>{}};

  EXPECT_TRUE(info.IsMultiFile());
  EXPECT_EQ(info.TotalLength(), 0);
}

TEST_F(TorrentTypesTest, InfoPrivateFlag) {
  Info const public_info{.name = "public", .piece_length = 16384};
  Info const private_info{
      .name = "private", .piece_length = 16384, .is_private = true};

  EXPECT_FALSE(public_info.is_private.has_value());
  ASSERT_TRUE(private_info.is_private.has_value());
  EXPECT_TRUE(*private_info.is_private);
}

// ============================================================================
// Torrent Tests
// ============================================================================

TEST_F(TorrentTypesTest, TorrentBasicConstruction) {
  Torrent const torrent{.announce = "http://tracker.example.com/announce",
                        .info = {.name = "test",
                                 .piece_length = 16384,
                                 .pieces = {MakeDummyHash()},
                                 .length = 1000}};

  EXPECT_EQ(torrent.announce, "http://tracker.example.com/announce");
  EXPECT_EQ(torrent.TotalLength(), 1000);
  EXPECT_EQ(torrent.PieceCount(), 1);
  EXPECT_FALSE(torrent.IsMultiFile());
}

TEST_F(TorrentTypesTest, TorrentWithOptionalMetadata) {
  auto const creation_time = std::chrono::system_clock::now();

  Torrent const torrent{
      .announce = "http://tracker.example.com/announce",
      .info = {.name = "test", .piece_length = 16384, .length = 1000},
      .announce_list =
          std::vector<std::vector<std::string>>{
              {"http://tracker1.com", "http://tracker2.com"}},
      .creation_date = creation_time,
      .comment = "Test torrent",
      .created_by = "ByteTorrent/1.0",
      .encoding = "UTF-8"};

  ASSERT_TRUE(torrent.announce_list.has_value());
  EXPECT_EQ(torrent.announce_list->size(), 1);
  EXPECT_EQ((*torrent.announce_list)[0].size(), 2);

  ASSERT_TRUE(torrent.creation_date.has_value());
  EXPECT_EQ(*torrent.creation_date, creation_time);

  ASSERT_TRUE(torrent.comment.has_value());
  EXPECT_EQ(*torrent.comment, "Test torrent");

  ASSERT_TRUE(torrent.created_by.has_value());
  EXPECT_EQ(*torrent.created_by, "ByteTorrent/1.0");
}

TEST_F(TorrentTypesTest, TorrentEquality) {
  Torrent const a{
      .announce = "http://tracker.com",
      .info = {.name = "test", .piece_length = 16384, .length = 100},
      .info_hash = MakeDummyHash(std::byte{0x01})};

  Torrent const b{
      .announce = "http://tracker.com",
      .info = {.name = "test", .piece_length = 16384, .length = 100},
      .info_hash = MakeDummyHash(std::byte{0x01})};

  Torrent const c{
      .announce = "http://other.com",
      .info = {.name = "test", .piece_length = 16384, .length = 100},
      .info_hash = MakeDummyHash(std::byte{0x02})};

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

// ============================================================================
// Validation Tests
// ============================================================================

TEST_F(TorrentTypesTest, ValidPieceLengthPowersOfTwo) {
  EXPECT_TRUE(validation::IsValidPieceLength(16384));     // 16 KB
  EXPECT_TRUE(validation::IsValidPieceLength(262144));    // 256 KB
  EXPECT_TRUE(validation::IsValidPieceLength(1048576));   // 1 MB
  EXPECT_TRUE(validation::IsValidPieceLength(16777216));  // 16 MB
}

TEST_F(TorrentTypesTest, InvalidPieceLengthNotPowerOfTwo) {
  EXPECT_FALSE(validation::IsValidPieceLength(0));
  EXPECT_FALSE(validation::IsValidPieceLength(1000));
  EXPECT_FALSE(validation::IsValidPieceLength(12345));
  EXPECT_FALSE(validation::IsValidPieceLength(262143));  // 256KB - 1
}

TEST_F(TorrentTypesTest, ValidPiecesData) {
  std::vector<std::byte> valid_20(20, std::byte{0x00});
  std::vector<std::byte> valid_40(40, std::byte{0x00});
  std::vector<std::byte> invalid_19(19, std::byte{0x00});
  std::vector<std::byte> invalid_21(21, std::byte{0x00});

  EXPECT_TRUE(validation::IsValidPiecesData(valid_20));
  EXPECT_TRUE(validation::IsValidPiecesData(valid_40));
  EXPECT_FALSE(validation::IsValidPiecesData(invalid_19));
  EXPECT_FALSE(validation::IsValidPiecesData(invalid_21));
  EXPECT_FALSE(validation::IsValidPiecesData({}));
}

TEST_F(TorrentTypesTest, ExpectedPieceCount) {
  // Exact division
  EXPECT_EQ(validation::ExpectedPieceCount(1048576, 262144), 4);

  // Partial last piece
  EXPECT_EQ(validation::ExpectedPieceCount(1000000, 262144), 4);

  // Single piece
  EXPECT_EQ(validation::ExpectedPieceCount(100, 16384), 1);

  // Edge case: zero piece length
  EXPECT_EQ(validation::ExpectedPieceCount(1000, 0), 0);

  // Edge case: zero total length
  EXPECT_EQ(validation::ExpectedPieceCount(0, 16384), 0);
}
