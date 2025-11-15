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

// ============================================================================
// DECODING
// ============================================================================

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

// ============================================================================
// ENCODING
// ============================================================================

// Integer encoding tests
TEST_F(BencodeTest, EncodePositiveInteger) {
  Value const value{Integer{42}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "i42e");
}

TEST_F(BencodeTest, EncodeNegativeInteger) {
  Value const value{Integer{-42}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "i-42e");
}

TEST_F(BencodeTest, EncodeZeroInteger) {
  Value const value{Integer{0}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "i0e");
}

TEST_F(BencodeTest, EncodeLargeInteger) {
  Value const value{Integer{INT64_MAX}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "i9223372036854775807e");
}

// ByteString encoding tests
TEST_F(BencodeTest, EncodeEmptyByteString) {
  Value const value{ByteString{}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "0:");
}

TEST_F(BencodeTest, EncodeSimpleByteString) {
  ByteString bytes(5);
  std::memcpy(bytes.data(), "hello", 5);
  Value const value{bytes};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "5:hello");
}

TEST_F(BencodeTest, EncodeByteStringWithBinaryData) {
  ByteString bytes{std::byte{0x00}, std::byte{0xFF}, std::byte{0x7F},
                   std::byte{0x80}};
  Value const value{bytes};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result.size(), 6);  // "4:" + 4 bytes
  EXPECT_EQ(result.substr(0, 2), "4:");
  EXPECT_EQ(static_cast<unsigned char>(result[2]), 0x00);
  EXPECT_EQ(static_cast<unsigned char>(result[3]), 0xFF);
  EXPECT_EQ(static_cast<unsigned char>(result[4]), 0x7F);
  EXPECT_EQ(static_cast<unsigned char>(result[5]), 0x80);
}

// List encoding tests
TEST_F(BencodeTest, EncodeEmptyList) {
  Value const value{List{}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "le");
}

TEST_F(BencodeTest, EncodeListWithIntegers) {
  List list{};
  list.push_back(Integer{1});
  list.push_back(Integer{2});
  list.push_back(Integer{3});
  Value const value{list};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "li1ei2ei3ee");
}

TEST_F(BencodeTest, EncodeListWithMixedTypes) {
  ByteString spam(4);
  std::memcpy(spam.data(), "spam", 4);

  ByteString egg(3);
  std::memcpy(egg.data(), "egg", 3);

  List list{};
  list.push_back(Integer{42});
  list.push_back(spam);
  list.push_back(egg);
  Value const value{list};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "li42e4:spam3:egge");
}

TEST_F(BencodeTest, EncodeNestedLists) {
  List inner1{};
  inner1.push_back(Integer{1});
  inner1.push_back(Integer{2});

  List inner2{};
  inner2.push_back(Integer{3});
  inner2.push_back(Integer{4});

  List outer{};
  outer.push_back(inner1);
  outer.push_back(inner2);
  Value const value{outer};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "lli1ei2eeli3ei4eee");
}

// Dictionary encoding tests
TEST_F(BencodeTest, EncodeEmptyDictionary) {
  Value const value{Dictionary{}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "de");
}

TEST_F(BencodeTest, EncodeSimpleDictionary) {
  ByteString moo(3);
  std::memcpy(moo.data(), "moo", 3);

  ByteString eggs(4);
  std::memcpy(eggs.data(), "eggs", 4);

  Dictionary dict{};
  dict["cow"] = moo;
  dict["spam"] = eggs;
  Value const value{dict};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "d3:cow3:moo4:spam4:eggse");
}

TEST_F(BencodeTest, EncodeDictionaryWithMixedValues) {
  ByteString alice(5);
  std::memcpy(alice.data(), "Alice", 5);

  Dictionary dict{};
  dict["age"] = Integer{25};
  dict["name"] = alice;
  Value const value{dict};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "d3:agei25e4:name5:Alicee");
}

TEST_F(BencodeTest, EncodeNestedDictionary) {
  ByteString bob(3);
  std::memcpy(bob.data(), "Bob", 3);

  Dictionary inner{};
  inner["age"] = Integer{30};
  inner["name"] = bob;

  Dictionary outer{};
  outer["user"] = inner;
  Value const value{outer};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "d4:userd3:agei30e4:name3:Bobee");
}

TEST_F(BencodeTest, EncodeDictionaryKeysAreSorted) {
  Dictionary dict{};
  dict["zebra"] = Integer{1};
  dict["apple"] = Integer{2};
  dict["mango"] = Integer{3};
  Value const value{dict};

  auto const result = EncodeIntoString(value);
  // std::map automatically sorts keys
  EXPECT_EQ(result, "d5:applei2e5:mangoi3e5:zebrai1ee");
}

// Complex structure test
TEST_F(BencodeTest, EncodeComplexTorrentLikeStructure) {
  ByteString announce(9);
  std::memcpy(announce.data(), "localhost", 9);

  ByteString name(8);
  std::memcpy(name.data(), "test.txt", 8);

  ByteString pieces(20);
  std::memcpy(pieces.data(), "01234567890123456789", 20);

  Dictionary info{};
  info["length"] = Integer{1024};
  info["name"] = name;
  info["piece length"] = Integer{16384};
  info["pieces"] = pieces;

  Dictionary torrent{};
  torrent["announce"] = announce;
  torrent["info"] = info;
  Value const value{torrent};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(
      result,
      "d8:announce9:localhost4:infod6:lengthi1024e4:name8:test.txt12:piece "
      "lengthi16384e6:pieces20:01234567890123456789ee");
}

// File encoding test
TEST_F(BencodeTest, EncodeIntoFile) {
  ByteString test1(6);
  std::memcpy(test1.data(), "test1", 5);

  Dictionary dict{};
  dict["name"] = test1;
  dict["size"] = Integer{12345};
  Value const value{dict};

  auto const file_path = test_dir / "encode_test.bencode";
  EncodeIntoFile(value, file_path);

  // Verify by decoding
  auto const decoded = DecodeFromFile(file_path);
  ASSERT_TRUE(std::holds_alternative<Dictionary>(decoded));
  auto const& result_dict = std::get<Dictionary>(decoded);
  EXPECT_EQ(std::get<Integer>(result_dict.at("size")), 12345);
}

// Span encoding test
TEST_F(BencodeTest, EncodeIntoSpan) {
  Value const value{Integer{42}};

  std::array<char, 10> buffer{};
  std::span<char> span{buffer};

  EncodeIntoSpan(value, span);

  std::string_view result{buffer.data(), 4};
  EXPECT_EQ(result, "i42e");
}

// Round-trip tests
TEST_F(BencodeTest, RoundTripInteger) {
  Value const original{Integer{12345}};
  auto const encoded = EncodeIntoString(original);
  auto const decoded = DecodeFromString(encoded);

  ASSERT_TRUE(std::holds_alternative<Integer>(decoded));
  EXPECT_EQ(std::get<Integer>(decoded), std::get<Integer>(original));
}

TEST_F(BencodeTest, RoundTripByteString) {
  ByteString bytes(11);
  std::memcpy(bytes.data(), "hello world", 11);
  Value const original{bytes};

  auto const encoded = EncodeIntoString(original);
  auto const decoded = DecodeFromString(encoded);

  ASSERT_TRUE(std::holds_alternative<ByteString>(decoded));
  EXPECT_EQ(std::get<ByteString>(decoded), std::get<ByteString>(original));
}

TEST_F(BencodeTest, RoundTripList) {
  List list{};
  list.push_back(Integer{1});
  list.push_back(Integer{2});
  list.push_back(Integer{3});
  Value const original{list};

  auto const encoded = EncodeIntoString(original);
  auto const decoded = DecodeFromString(encoded);

  ASSERT_TRUE(std::holds_alternative<List>(decoded));
  auto const& result_list = std::get<List>(decoded);
  ASSERT_EQ(result_list.size(), 3);
  EXPECT_EQ(std::get<Integer>(result_list[0]), 1);
  EXPECT_EQ(std::get<Integer>(result_list[1]), 2);
  EXPECT_EQ(std::get<Integer>(result_list[2]), 3);
}

TEST_F(BencodeTest, RoundTripDictionary) {
  ByteString value1(5);
  std::memcpy(value1.data(), "value", 5);

  Dictionary dict{};
  dict["key"] = value1;
  dict["num"] = Integer{999};
  Value const original{dict};

  auto const encoded = EncodeIntoString(original);
  auto const decoded = DecodeFromString(encoded);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(decoded));
  auto const& result_dict = std::get<Dictionary>(decoded);
  EXPECT_EQ(result_dict.size(), 2);
  EXPECT_EQ(std::get<Integer>(result_dict.at("num")), 999);
}

TEST_F(BencodeTest, RoundTripComplexStructure) {
  ByteString str1(4);
  std::memcpy(str1.data(), "test", 4);

  List inner_list{};
  inner_list.push_back(Integer{1});
  inner_list.push_back(str1);

  Dictionary inner_dict{};
  inner_dict["x"] = Integer{100};

  List outer_list{};
  outer_list.push_back(inner_list);
  outer_list.push_back(inner_dict);

  Dictionary outer_dict{};
  outer_dict["data"] = outer_list;
  outer_dict["version"] = Integer{2};
  Value const original{outer_dict};

  auto const encoded = EncodeIntoString(original);
  auto const decoded = DecodeFromString(encoded);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(decoded));
  auto const& result = std::get<Dictionary>(decoded);
  EXPECT_EQ(std::get<Integer>(result.at("version")), 2);
}

// Error handling tests
TEST_F(BencodeTest, EncodeMonostateThrows) {
  Value const value{std::monostate{}};
  EXPECT_THROW(EncodeIntoString(value), std::invalid_argument);
}

TEST_F(BencodeTest, EncodeIntoNonExistentDirectory) {
  Value const value{Integer{42}};
  auto const bad_path = std::filesystem::path{"/non/existent/dir/file.bencode"};
  EXPECT_THROW(EncodeIntoFile(value, bad_path), std::runtime_error);
}

// Edge cases
TEST_F(BencodeTest, EncodeLargeByteString) {
  ByteString bytes(1000, std::byte{'x'});
  Value const value{bytes};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result.size(), 1005);  // "1000:" + 1000 bytes
  EXPECT_EQ(result.substr(0, 5), "1000:");
}

TEST_F(BencodeTest, EncodeEmptyDictionaryKey) {
  ByteString empty_value(5);
  std::memcpy(empty_value.data(), "value", 5);

  Dictionary dict{};
  dict[""] = empty_value;  // Empty key
  Value const value{dict};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "d0:5:valuee");
}
