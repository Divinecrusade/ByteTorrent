#include <gtest/gtest.h>
import bencode;
import torrent;
#include <array>
#include <chrono>
#include <span>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace byte_torrent::torrent;
using namespace byte_torrent::bencode;
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

// ============================================================================
// Test Fixture
// ============================================================================

class TorrentHashTest : public ::testing::Test {
 protected:
  static std::vector<std::byte> ToBytes(std::string_view str) {
    std::vector<std::byte> result(str.size());
    std::transform(str.begin(), str.end(), result.begin(),
                   [](char c) { return static_cast<std::byte>(c); });
    return result;
  }
};

// ============================================================================
// SHA1 Calculation Tests - Known Test Vectors
// ============================================================================

TEST_F(TorrentHashTest, Sha1EmptyString) {
  // SHA1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
  auto const hash = CalculateSha1(std::span<char const>{});
  EXPECT_EQ(Sha1ToHex(hash), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

TEST_F(TorrentHashTest, Sha1SimpleString) {
  // SHA1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
  std::string_view const input{"abc"};
  auto const hash = CalculateSha1(std::span{input.data(), input.size()});
  EXPECT_EQ(Sha1ToHex(hash), "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST_F(TorrentHashTest, Sha1LongerString) {
  // SHA1("The quick brown fox jumps over the lazy dog")
  // = 2fd4e1c67a2d28fced849ee1bb76e7391b93eb12
  std::string_view const input{"The quick brown fox jumps over the lazy dog"};
  auto const hash = CalculateSha1(std::span{input.data(), input.size()});
  EXPECT_EQ(Sha1ToHex(hash), "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
}

TEST_F(TorrentHashTest, Sha1ByteSpanOverload) {
  auto const bytes = ToBytes("abc");
  auto const hash = CalculateSha1(std::span<std::byte const>{bytes});
  EXPECT_EQ(Sha1ToHex(hash), "a9993e364706816aba3e25717850c26c9cd0d89d");
}

// ============================================================================
// Hex Conversion Tests
// ============================================================================

TEST_F(TorrentHashTest, Sha1ToHexFormat) {
  Sha1Hash hash{};
  std::ranges::fill(hash, std::byte{0x00});
  EXPECT_EQ(Sha1ToHex(hash), "0000000000000000000000000000000000000000");

  std::ranges::fill(hash, std::byte{0xFF});
  EXPECT_EQ(Sha1ToHex(hash), "ffffffffffffffffffffffffffffffffffffffff");

  hash[0] = std::byte{0x0A};
  hash[19] = std::byte{0xBC};
  EXPECT_EQ(Sha1ToHex(hash), "0affffffffffffffffffffffffffffffffffffbc");
}

TEST_F(TorrentHashTest, Sha1FromHexValid) {
  auto const hash = Sha1FromHex("a9993e364706816aba3e25717850c26c9cd0d89d");

  EXPECT_EQ(hash[0], std::byte{0xA9});
  EXPECT_EQ(hash[1], std::byte{0x99});
  EXPECT_EQ(hash[19], std::byte{0x9D});
}

TEST_F(TorrentHashTest, Sha1FromHexUpperCase) {
  auto const lower = Sha1FromHex("a9993e364706816aba3e25717850c26c9cd0d89d");
  auto const upper = Sha1FromHex("A9993E364706816ABA3E25717850C26C9CD0D89D");
  EXPECT_EQ(lower, upper);
}

TEST_F(TorrentHashTest, Sha1HexRoundTrip) {
  std::string_view const original{"da39a3ee5e6b4b0d3255bfef95601890afd80709"};
  auto const hash = Sha1FromHex(original);
  auto const hex = Sha1ToHex(hash);
  EXPECT_EQ(hex, original);
}

// ============================================================================
// Hex Conversion Error Handling
// ============================================================================

TEST_F(TorrentHashTest, Sha1FromHexInvalidLength) {
  EXPECT_THROW(Sha1FromHex("a9993e"), std::invalid_argument);
  EXPECT_THROW(Sha1FromHex("a9993e364706816aba3e25717850c26c9cd0d89d00"),
               std::invalid_argument);
  EXPECT_THROW(Sha1FromHex(""), std::invalid_argument);
}

TEST_F(TorrentHashTest, Sha1FromHexInvalidCharacters) {
  EXPECT_THROW(Sha1FromHex("g9993e364706816aba3e25717850c26c9cd0d89d"),
               std::invalid_argument);
  EXPECT_THROW(Sha1FromHex("a9993e364706816aba3e25717850c26c9cd0d8!!"),
               std::invalid_argument);
}

// ============================================================================
// Info Hash Calculation Tests
// ============================================================================

TEST_F(TorrentHashTest, CalculateInfoHashSimpleDictionary) {
  // Create a simple info dictionary
  ByteString name(4);
  std::memcpy(name.data(), "test", 4);

  Dictionary info{};
  info["name"] = name;
  info["piece length"] = Integer{16384};
  info["length"] = Integer{1024};

  auto const hash = CalculateInfoHash(info);

  // Verify by manually encoding and hashing
  auto const encoded = EncodeIntoString(Value{info});
  auto const expected =
      CalculateSha1(std::span{encoded.data(), encoded.size()});

  EXPECT_EQ(hash, expected);
}

TEST_F(TorrentHashTest, CalculateInfoHashDeterministic) {
  ByteString name(8);
  std::memcpy(name.data(), "test.txt", 8);

  Dictionary info{};
  info["name"] = name;
  info["piece length"] = Integer{262144};
  info["length"] = Integer{999999};

  auto const hash1 = CalculateInfoHash(info);
  auto const hash2 = CalculateInfoHash(info);

  EXPECT_EQ(hash1, hash2);
}

TEST_F(TorrentHashTest, CalculateInfoHashDifferentContentDifferentHash) {
  ByteString name1(5);
  std::memcpy(name1.data(), "file1", 5);

  ByteString name2(5);
  std::memcpy(name2.data(), "file2", 5);

  Dictionary info1{};
  info1["name"] = name1;
  info1["length"] = Integer{100};

  Dictionary info2{};
  info2["name"] = name2;
  info2["length"] = Integer{100};

  EXPECT_NE(CalculateInfoHash(info1), CalculateInfoHash(info2));
}

TEST_F(TorrentHashTest, CalculateInfoHashEmptyDictionary) {
  Dictionary const empty{};
  auto const hash = CalculateInfoHash(empty);

  // SHA1("de") - empty bencoded dictionary
  std::string_view const encoded{"de"};
  auto const expected =
      CalculateSha1(std::span{encoded.data(), encoded.size()});

  EXPECT_EQ(hash, expected);
}

// ============================================================================
// Test Fixture
// ============================================================================

class TorrentParseTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = std::filesystem::temp_directory_path() / "torrent_parse_tests";
    std::filesystem::create_directories(test_dir_);
  }

  void TearDown() override { std::filesystem::remove_all(test_dir_); }

  static ByteString ToByteString(std::string_view str) {
    ByteString result(str.size());
    std::memcpy(result.data(), str.data(), str.size());
    return result;
  }

  // Creates 20-byte pieces data (1 piece hash)
  static ByteString MakePieces(std::size_t piece_count = 1) {
    ByteString pieces(piece_count * 20);
    std::ranges::fill(pieces, std::byte{0xAB});
    return pieces;
  }

  static Dictionary MakeMinimalInfo(std::string_view name = "test.txt",
                                    Integer length = 1024) {
    Dictionary info{};
    info["name"] = ToByteString(name);
    info["piece length"] = Integer{16384};
    info["pieces"] = MakePieces();
    info["length"] = length;
    return info;
  }

  static Dictionary MakeMinimalTorrent(
      std::string_view announce = "http://tracker.example.com/announce") {
    Dictionary torrent{};
    torrent["announce"] = ToByteString(announce);
    torrent["info"] = MakeMinimalInfo();
    return torrent;
  }

  std::filesystem::path CreateTestFile(std::string const& content) {
    static int counter = 0;
    auto path = test_dir_ / ("test_" + std::to_string(counter++) + ".torrent");
    std::ofstream file{path, std::ios::binary};
    file << content;
    return path;
  }

  std::filesystem::path test_dir_;
};

// ============================================================================
// Single-File Torrent Tests
// ============================================================================

TEST_F(TorrentParseTest, ParseMinimalSingleFileTorrent) {
  auto const encoded = EncodeIntoString(Value{MakeMinimalTorrent()});
  auto const torrent = ParseFromString(encoded);

  EXPECT_EQ(torrent.announce, "http://tracker.example.com/announce");
  EXPECT_EQ(torrent.info.name, "test.txt");
  EXPECT_EQ(torrent.info.piece_length, 16384);
  EXPECT_EQ(torrent.info.PieceCount(), 1);
  EXPECT_TRUE(torrent.info.IsSingleFile());
  EXPECT_FALSE(torrent.info.IsMultiFile());
  ASSERT_TRUE(torrent.info.length.has_value());
  EXPECT_EQ(*torrent.info.length, 1024);
}

TEST_F(TorrentParseTest, ParseSingleFileTorrentInfoHash) {
  auto dict = MakeMinimalTorrent();
  auto const encoded = EncodeIntoString(Value{dict});
  auto const torrent = ParseFromString(encoded);

  // Verify info_hash matches manual calculation
  auto const& info_dict = std::get<Dictionary>(dict["info"]);
  auto const expected_hash = CalculateInfoHash(info_dict);

  EXPECT_EQ(torrent.info_hash, expected_hash);
}

TEST_F(TorrentParseTest, ParseFromFile) {
  auto const content = EncodeIntoString(Value{MakeMinimalTorrent()});
  auto const path = CreateTestFile(content);

  auto const torrent = ParseFromFile(path);

  EXPECT_EQ(torrent.announce, "http://tracker.example.com/announce");
  EXPECT_EQ(torrent.info.name, "test.txt");
}

TEST_F(TorrentParseTest, ParseFromSpan) {
  auto const content = EncodeIntoString(Value{MakeMinimalTorrent()});
  std::span<char const> span{content.data(), content.size()};

  auto const torrent = ParseFromSpan(span);

  EXPECT_EQ(torrent.announce, "http://tracker.example.com/announce");
}

// ============================================================================
// Multi-File Torrent Tests
// ============================================================================

TEST_F(TorrentParseTest, ParseMultiFileTorrent) {
  Dictionary file1{};
  file1["length"] = Integer{100};
  file1["path"] = List{ToByteString("file1.txt")};

  Dictionary file2{};
  file2["length"] = Integer{200};
  file2["path"] = List{ToByteString("subdir"), ToByteString("file2.txt")};

  Dictionary info{};
  info["name"] = ToByteString("my_torrent");
  info["piece length"] = Integer{16384};
  info["pieces"] = MakePieces();
  info["files"] = List{file1, file2};

  Dictionary root{};
  root["announce"] = ToByteString("http://tracker.com/announce");
  root["info"] = info;

  auto const encoded = EncodeIntoString(Value{root});
  auto const torrent = ParseFromString(encoded);

  EXPECT_TRUE(torrent.info.IsMultiFile());
  EXPECT_FALSE(torrent.info.IsSingleFile());
  ASSERT_TRUE(torrent.info.files.has_value());
  ASSERT_EQ(torrent.info.files->size(), 2);

  EXPECT_EQ((*torrent.info.files)[0].path, "file1.txt");
  EXPECT_EQ((*torrent.info.files)[0].length, 100);

  EXPECT_EQ((*torrent.info.files)[1].path, "subdir/file2.txt");
  EXPECT_EQ((*torrent.info.files)[1].length, 200);

  EXPECT_EQ(torrent.TotalLength(), 300);
}

TEST_F(TorrentParseTest, ParseMultiFileTorrentWithMd5sum) {
  Dictionary file{};
  file["length"] = Integer{1024};
  file["path"] = List{ToByteString("file.bin")};
  file["md5sum"] = ToByteString("d41d8cd98f00b204e9800998ecf8427e");

  Dictionary info{};
  info["name"] = ToByteString("torrent_with_md5");
  info["piece length"] = Integer{16384};
  info["pieces"] = MakePieces();
  info["files"] = List{file};

  Dictionary root{};
  root["announce"] = ToByteString("http://tracker.com/announce");
  root["info"] = info;

  auto const encoded = EncodeIntoString(Value{root});
  auto const torrent = ParseFromString(encoded);

  ASSERT_TRUE(torrent.info.files.has_value());
  ASSERT_EQ(torrent.info.files->size(), 1);
  ASSERT_TRUE((*torrent.info.files)[0].md5sum.has_value());
  EXPECT_EQ(*(*torrent.info.files)[0].md5sum,
            "d41d8cd98f00b204e9800998ecf8427e");
}

// ============================================================================
// Optional Metadata Tests
// ============================================================================

TEST_F(TorrentParseTest, ParseTorrentWithAllOptionalFields) {
  auto root = MakeMinimalTorrent();
  root["comment"] = ToByteString("Test comment");
  root["created by"] = ToByteString("ByteTorrent/1.0");
  root["creation date"] = Integer{1700000000};
  root["encoding"] = ToByteString("UTF-8");

  auto const encoded = EncodeIntoString(Value{root});
  auto const torrent = ParseFromString(encoded);

  ASSERT_TRUE(torrent.comment.has_value());
  EXPECT_EQ(*torrent.comment, "Test comment");

  ASSERT_TRUE(torrent.created_by.has_value());
  EXPECT_EQ(*torrent.created_by, "ByteTorrent/1.0");

  ASSERT_TRUE(torrent.creation_date.has_value());
  auto const expected_time = std::chrono::system_clock::from_time_t(1700000000);
  EXPECT_EQ(*torrent.creation_date, expected_time);

  ASSERT_TRUE(torrent.encoding.has_value());
  EXPECT_EQ(*torrent.encoding, "UTF-8");
}

TEST_F(TorrentParseTest, ParseTorrentWithAnnounceList) {
  auto root = MakeMinimalTorrent();

  List tier1{ToByteString("http://tracker1.com"),
             ToByteString("http://tracker2.com")};
  List tier2{ToByteString("http://backup.com")};
  root["announce-list"] = List{tier1, tier2};

  auto const encoded = EncodeIntoString(Value{root});
  auto const torrent = ParseFromString(encoded);

  ASSERT_TRUE(torrent.announce_list.has_value());
  ASSERT_EQ(torrent.announce_list->size(), 2);

  EXPECT_EQ((*torrent.announce_list)[0].size(), 2);
  EXPECT_EQ((*torrent.announce_list)[0][0], "http://tracker1.com");
  EXPECT_EQ((*torrent.announce_list)[0][1], "http://tracker2.com");

  EXPECT_EQ((*torrent.announce_list)[1].size(), 1);
  EXPECT_EQ((*torrent.announce_list)[1][0], "http://backup.com");
}

TEST_F(TorrentParseTest, ParseTorrentWithPrivateFlag) {
  auto root = MakeMinimalTorrent();
  auto& info = std::get<Dictionary>(root["info"]);
  info["private"] = Integer{1};

  auto const encoded = EncodeIntoString(Value{root});
  auto const torrent = ParseFromString(encoded);

  ASSERT_TRUE(torrent.info.is_private.has_value());
  EXPECT_TRUE(*torrent.info.is_private);
}

TEST_F(TorrentParseTest, ParseTorrentPrivateFlagZero) {
  auto root = MakeMinimalTorrent();
  auto& info = std::get<Dictionary>(root["info"]);
  info["private"] = Integer{0};

  auto const encoded = EncodeIntoString(Value{root});
  auto const torrent = ParseFromString(encoded);

  ASSERT_TRUE(torrent.info.is_private.has_value());
  EXPECT_FALSE(*torrent.info.is_private);
}

// ============================================================================
// Pieces Parsing Tests
// ============================================================================

TEST_F(TorrentParseTest, ParseMultiplePieces) {
  auto root = MakeMinimalTorrent();
  auto& info = std::get<Dictionary>(root["info"]);
  info["pieces"] = MakePieces(5);

  auto const encoded = EncodeIntoString(Value{root});
  auto const torrent = ParseFromString(encoded);

  EXPECT_EQ(torrent.info.PieceCount(), 5);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(TorrentParseTest, ThrowsOnNonDictionaryRoot) {
  auto const encoded = EncodeIntoString(Value{Integer{42}});
  EXPECT_THROW(ParseFromString(encoded), std::invalid_argument);
}

TEST_F(TorrentParseTest, ThrowsOnMissingAnnounce) {
  Dictionary root{};
  root["info"] = MakeMinimalInfo();

  auto const encoded = EncodeIntoString(Value{root});
  EXPECT_THROW(ParseFromString(encoded), std::invalid_argument);
}

TEST_F(TorrentParseTest, ThrowsOnMissingInfo) {
  Dictionary root{};
  root["announce"] = ToByteString("http://tracker.com/announce");

  auto const encoded = EncodeIntoString(Value{root});
  EXPECT_THROW(ParseFromString(encoded), std::invalid_argument);
}

TEST_F(TorrentParseTest, ThrowsOnMissingName) {
  Dictionary info{};
  info["piece length"] = Integer{16384};
  info["pieces"] = MakePieces();
  info["length"] = Integer{1024};

  Dictionary root{};
  root["announce"] = ToByteString("http://tracker.com/announce");
  root["info"] = info;

  auto const encoded = EncodeIntoString(Value{root});
  EXPECT_THROW(ParseFromString(encoded), std::invalid_argument);
}

TEST_F(TorrentParseTest, ThrowsOnMissingPieceLength) {
  Dictionary info{};
  info["name"] = ToByteString("test.txt");
  info["pieces"] = MakePieces();
  info["length"] = Integer{1024};

  Dictionary root{};
  root["announce"] = ToByteString("http://tracker.com/announce");
  root["info"] = info;

  auto const encoded = EncodeIntoString(Value{root});
  EXPECT_THROW(ParseFromString(encoded), std::invalid_argument);
}

TEST_F(TorrentParseTest, ThrowsOnMissingPieces) {
  Dictionary info{};
  info["name"] = ToByteString("test.txt");
  info["piece length"] = Integer{16384};
  info["length"] = Integer{1024};

  Dictionary root{};
  root["announce"] = ToByteString("http://tracker.com/announce");
  root["info"] = info;

  auto const encoded = EncodeIntoString(Value{root});
  EXPECT_THROW(ParseFromString(encoded), std::invalid_argument);
}

TEST_F(TorrentParseTest, ThrowsOnMissingLengthAndFiles) {
  Dictionary info{};
  info["name"] = ToByteString("test.txt");
  info["piece length"] = Integer{16384};
  info["pieces"] = MakePieces();

  Dictionary root{};
  root["announce"] = ToByteString("http://tracker.com/announce");
  root["info"] = info;

  auto const encoded = EncodeIntoString(Value{root});
  EXPECT_THROW(ParseFromString(encoded), std::invalid_argument);
}

TEST_F(TorrentParseTest, ThrowsOnInvalidPiecesLength) {
  auto root = MakeMinimalTorrent();
  auto& info = std::get<Dictionary>(root["info"]);
  info["pieces"] = ByteString(19, std::byte{0xAB});  // Not multiple of 20

  auto const encoded = EncodeIntoString(Value{root});
  EXPECT_THROW(ParseFromString(encoded), std::invalid_argument);
}

TEST_F(TorrentParseTest, ThrowsOnNegativeLength) {
  auto root = MakeMinimalTorrent();
  auto& info = std::get<Dictionary>(root["info"]);
  info["length"] = Integer{-100};

  auto const encoded = EncodeIntoString(Value{root});
  EXPECT_THROW(ParseFromString(encoded), std::invalid_argument);
}

TEST_F(TorrentParseTest, ThrowsOnNegativePieceLength) {
  auto root = MakeMinimalTorrent();
  auto& info = std::get<Dictionary>(root["info"]);
  info["piece length"] = Integer{-16384};

  auto const encoded = EncodeIntoString(Value{root});
  EXPECT_THROW(ParseFromString(encoded), std::invalid_argument);
}

TEST_F(TorrentParseTest, ThrowsOnEmptyFilePath) {
  Dictionary file{};
  file["length"] = Integer{100};
  file["path"] = List{};  // Empty path

  Dictionary info{};
  info["name"] = ToByteString("torrent");
  info["piece length"] = Integer{16384};
  info["pieces"] = MakePieces();
  info["files"] = List{file};

  Dictionary root{};
  root["announce"] = ToByteString("http://tracker.com/announce");
  root["info"] = info;

  auto const encoded = EncodeIntoString(Value{root});
  EXPECT_THROW(ParseFromString(encoded), std::invalid_argument);
}

TEST_F(TorrentParseTest, ThrowsOnNonExistentFile) {
  EXPECT_THROW(ParseFromFile("/non/existent/file.torrent"), std::runtime_error);
}
