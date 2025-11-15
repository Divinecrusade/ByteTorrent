#include <gtest/gtest.h>
import bencode;
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <string_view>

using namespace byte_torrent::bencode;
using namespace std::string_literals;
using namespace std::string_view_literals;

class BencodeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir = std::filesystem::temp_directory_path() / "bencode_tests";
    std::filesystem::create_directories(test_dir);
  }

  void TearDown() override { std::filesystem::remove_all(test_dir); }

  std::filesystem::path CreateTestFile(std::string const& content) {
    static int file_counter = 0;
    auto path =
        test_dir / ("test_" + std::to_string(file_counter++) + ".bencode");
    std::ofstream file{path, std::ios::binary};
    file << content;
    return path;
  }

  std::filesystem::path test_dir;
};

// Integer decoding tests
TEST_F(BencodeTest, DecodePositiveInteger) {
  constexpr std::string_view data{"i42e"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), 42);
}

TEST_F(BencodeTest, DecodeNegativeInteger) {
  constexpr std::string_view data{"i-42e"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), -42);
}

TEST_F(BencodeTest, DecodeZeroInteger) {
  constexpr std::string_view data{"i0e"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), 0);
}

TEST_F(BencodeTest, DecodeLargeInteger) {
  constexpr std::string_view data{"i9223372036854775807e"};  // INT64_MAX
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), INT64_MAX);
}

TEST(BencodeDecodeFromSpan, DecodesInteger) {
  std::string data{"i42e"};
  std::span<char> range{data.data(), data.size()};

  Value result{DecodeFromSpan(range)};

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), 42);
}

// ByteString decoding tests
TEST_F(BencodeTest, DecodeEmptyByteString) {
  constexpr std::string_view data{"0:"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<ByteString>(result));
  EXPECT_TRUE(std::get<ByteString>(result).empty());
}

TEST_F(BencodeTest, DecodeSimpleByteString) {
  constexpr std::string_view data{"5:hello"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<ByteString>(result));
  auto const& bytes = std::get<ByteString>(result);
  std::string decoded{reinterpret_cast<char const*>(bytes.data()),
                      bytes.size()};
  EXPECT_EQ(decoded, "hello");
}

TEST_F(BencodeTest, DecodeByteStringWithBinaryData) {
  std::string content(4, '\0');
  content[0] = '\x00';
  content[1] = '\xFF';
  content[2] = '\x7F';
  content[3] = '\x80';
  std::string data = "4:" + content;
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<ByteString>(result));
  auto const& bytes = std::get<ByteString>(result);
  EXPECT_EQ(bytes.size(), 4);
  EXPECT_EQ(bytes[0], std::byte{0x00});
  EXPECT_EQ(bytes[1], std::byte{0xFF});
  EXPECT_EQ(bytes[2], std::byte{0x7F});
  EXPECT_EQ(bytes[3], std::byte{0x80});
}

// List decoding tests
TEST_F(BencodeTest, DecodeEmptyList) {
  constexpr std::string_view data{"le"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<List>(result));
  EXPECT_TRUE(std::get<List>(result).empty());
}

TEST_F(BencodeTest, DecodeListWithIntegers) {
  constexpr std::string_view data{"li1ei2ei3ee"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<List>(result));
  auto const& list = std::get<List>(result);
  ASSERT_EQ(list.size(), 3);

  EXPECT_EQ(std::get<Integer>(list[0]), 1);
  EXPECT_EQ(std::get<Integer>(list[1]), 2);
  EXPECT_EQ(std::get<Integer>(list[2]), 3);
}

TEST_F(BencodeTest, DecodeListWithMixedTypes) {
  constexpr std::string_view data{"li42e4:spam3:egge"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<List>(result));
  auto const& list = std::get<List>(result);
  ASSERT_EQ(list.size(), 3);

  EXPECT_EQ(std::get<Integer>(list[0]), 42);

  auto const& str1 = std::get<ByteString>(list[1]);
  std::string const decoded1{reinterpret_cast<char const*>(str1.data()), str1.size()};
  EXPECT_EQ(decoded1, "spam");

  auto const& str2 = std::get<ByteString>(list[2]);
  std::string const decoded2{reinterpret_cast<char const*>(str2.data()), str2.size()};
  EXPECT_EQ(decoded2, "egg");
}

TEST(BencodeDecodeFromSpan, DecodesByteString) {
  std::string data{"4:spam"};
  std::span<char> range{data.data(), data.size()};

  auto const result = DecodeFromSpan(range);

  ASSERT_TRUE(std::holds_alternative<ByteString>(result));
  auto const& bytes = std::get<ByteString>(result);
  std::string const decoded{reinterpret_cast<char const*>(bytes.data()),
                            bytes.size()};
  EXPECT_EQ(decoded, "spam");
}

TEST(BencodeDecodeFromSpan, DecodesList) {
  constexpr std::string_view data{"li42e4:spame"};
  constexpr std::span<char const> range{data.data(), data.size()};

  auto const result = DecodeFromSpan(range);

  ASSERT_TRUE(std::holds_alternative<List>(result));
  auto const& list = std::get<List>(result);
  ASSERT_EQ(list.size(), 2);
  EXPECT_EQ(std::get<Integer>(list[0]), 42);

  auto const& bytes = std::get<ByteString>(list[1]);
  std::string const decoded(reinterpret_cast<char const*>(bytes.data()),
                            bytes.size());
  EXPECT_EQ(decoded, "spam");
}

TEST_F(BencodeTest, DecodeNestedLists) {
  constexpr std::string_view data{"lli1ei2eeli3ei4eee"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<List>(result));
  auto const& outer_list = std::get<List>(result);
  ASSERT_EQ(outer_list.size(), 2);

  auto const& inner_list1 = std::get<List>(outer_list[0]);
  ASSERT_EQ(inner_list1.size(), 2);
  EXPECT_EQ(std::get<Integer>(inner_list1[0]), 1);
  EXPECT_EQ(std::get<Integer>(inner_list1[1]), 2);

  auto const& inner_list2 = std::get<List>(outer_list[1]);
  ASSERT_EQ(inner_list2.size(), 2);
  EXPECT_EQ(std::get<Integer>(inner_list2[0]), 3);
  EXPECT_EQ(std::get<Integer>(inner_list2[1]), 4);
}

// Dictionary decoding tests
TEST_F(BencodeTest, DecodeEmptyDictionary) {
  constexpr std::string_view data{"de"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(result));
  EXPECT_TRUE(std::get<Dictionary>(result).empty());
}

TEST_F(BencodeTest, DecodeSimpleDictionary) {
  constexpr std::string_view data{"d3:cow3:moo4:spam4:eggse"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(result));
  auto const& dict = std::get<Dictionary>(result);
  ASSERT_EQ(dict.size(), 2);

  auto const cow_it = dict.find("cow");
  ASSERT_NE(cow_it, dict.end());
  auto const& cow_value = std::get<ByteString>(cow_it->second);
  std::string cow_str{reinterpret_cast<char const*>(cow_value.data()),
                      cow_value.size()};
  EXPECT_EQ(cow_str, "moo");

  auto const spam_it = dict.find("spam");
  ASSERT_NE(spam_it, dict.end());
  auto const& spam_value = std::get<ByteString>(spam_it->second);
  std::string spam_str{reinterpret_cast<char const*>(spam_value.data()),
                       spam_value.size()};
  EXPECT_EQ(spam_str, "eggs");
}

TEST_F(BencodeTest, DecodeDictionaryWithMixedValues) {
  constexpr std::string_view data{"d3:agei25e4:name5:Alicee"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(result));
  auto const& dict = std::get<Dictionary>(result);
  ASSERT_EQ(dict.size(), 2);

  auto const age_it = dict.find("age");
  ASSERT_NE(age_it, dict.end());
  EXPECT_EQ(std::get<Integer>(age_it->second), 25);

  auto const name_it = dict.find("name");
  ASSERT_NE(name_it, dict.end());
  auto const& name_value = std::get<ByteString>(name_it->second);
  std::string const name_str{reinterpret_cast<char const*>(name_value.data()),
                             name_value.size()};
  EXPECT_EQ(name_str, "Alice");
}

TEST_F(BencodeTest, DecodeNestedDictionary) {
  constexpr std::string_view data{"d4:userd3:agei30e4:name3:Bobee"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(result));
  auto const& outer_dict = std::get<Dictionary>(result);
  ASSERT_EQ(outer_dict.size(), 1);

  auto const user_it = outer_dict.find("user");
  ASSERT_NE(user_it, outer_dict.end());

  auto const& inner_dict = std::get<Dictionary>(user_it->second);
  ASSERT_EQ(inner_dict.size(), 2);

  auto const age_it = inner_dict.find("age");
  ASSERT_NE(age_it, inner_dict.end());
  EXPECT_EQ(std::get<Integer>(age_it->second), 30);
}

// Complex structure test
TEST_F(BencodeTest, DecodeComplexTorrentLikeStructure) {
  constexpr std::string_view data{
      "d8:announce9:localhost4:infod6:lengthi1024e4:name8:test.txt12:piece "
      "lengthi16384e6:pieces20:01234567890123456789ee"};
  auto result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(result));
  auto const& torrent = std::get<Dictionary>(result);

  EXPECT_TRUE(torrent.contains("announce"));
  EXPECT_TRUE(torrent.contains("info"));

  auto const& info = std::get<Dictionary>(torrent.at("info"));
  EXPECT_EQ(std::get<Integer>(info.at("length")), 1024);
  EXPECT_EQ(std::get<Integer>(info.at("piece length")), 16384);
}

// File-based decoding test
TEST_F(BencodeTest, DecodeFromFile) {
  std::string const content{"d4:name5:test14:sizei12345ee"};
  auto const file_path = CreateTestFile(content);

  auto const result = DecodeFromFile(file_path);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(result));
  auto const& dict = std::get<Dictionary>(result);
  EXPECT_EQ(dict.size(), 2);
  EXPECT_EQ(std::get<Integer>(dict.at("size")), 12345);
}

// Error handling tests
TEST_F(BencodeTest, InvalidIntegerFormat) {
  EXPECT_THROW(DecodeFromString("i12x34e"), std::invalid_argument);
}

TEST_F(BencodeTest, MissingIntegerEndMarker) {
  EXPECT_THROW(DecodeFromString("i123"), std::runtime_error);
}

TEST_F(BencodeTest, InvalidByteStringLength) {
  // Length says 5 but only 3 bytes
  EXPECT_THROW(DecodeFromString("5:abc"sv), std::runtime_error);
}

TEST_F(BencodeTest, InvalidByteStringDelimiter) {
  // Using ';' instead of ':'
  EXPECT_THROW(DecodeFromString("3;abc"s), std::invalid_argument);
}

TEST_F(BencodeTest, DuplicateKeyInDictionary) {
  EXPECT_THROW(DecodeFromString("d3:key5:value3:key6:value2e"),
                                std::invalid_argument);
}

TEST_F(BencodeTest, NonExistentFile) {
  EXPECT_THROW(DecodeFromFile("/non/existent/file.bencode"),
                              std::runtime_error);
}

TEST_F(BencodeTest, EmptyString) {
  constexpr std::string_view data{""};
  EXPECT_THROW(DecodeFromString(data), std::invalid_argument);
}

TEST(BencodeDecodeFromSpan, ThrowsOnEmptyRange) {
  EXPECT_THROW(DecodeFromSpan({}), std::invalid_argument);
}

// Edge case tests
TEST_F(BencodeTest, DecodeLeadingZeroInInteger) {
  constexpr std::string_view data{"i-0e"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), 0);
}

TEST_F(BencodeTest, DecodeLargeByteString) {
  std::string const content(1000, 'x');
  std::string const data = "1000:" + content;
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<ByteString>(result));
  auto const& bytes = std::get<ByteString>(result);
  EXPECT_EQ(bytes.size(), 1000);
  EXPECT_TRUE(std::all_of(bytes.begin(), bytes.end(),
                          [](std::byte b) { return b == std::byte{'x'}; }));
}