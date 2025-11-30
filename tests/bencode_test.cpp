#include <gtest/gtest.h>
import bencode;
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace byte_torrent::bencode;
using namespace std::string_literals;
using namespace std::string_view_literals;

// ============================================================================
// Base Test Fixture
// ============================================================================

class BencodeTestBase : public ::testing::Test {
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

class BencodeDecoding : public BencodeTestBase {};
class BencodeEncoding : public BencodeTestBase {};

// ============================================================================
// DECODING TESTS
// ============================================================================

// ----------------------------------------------------------------------------
// Integer Decoding
// ----------------------------------------------------------------------------

TEST_F(BencodeDecoding, DecodePositiveInteger) {
  constexpr std::string_view data{"i42e"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), 42);
}

TEST_F(BencodeDecoding, DecodeNegativeInteger) {
  constexpr std::string_view data{"i-42e"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), -42);
}

TEST_F(BencodeDecoding, DecodeZeroInteger) {
  constexpr std::string_view data{"i0e"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), 0);
}

TEST_F(BencodeDecoding, DecodeLargeInteger) {
  constexpr std::string_view data{"i9223372036854775807e"};  // INT64_MAX
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), INT64_MAX);
}

TEST_F(BencodeDecoding, DecodeLeadingZeroInInteger) {
  constexpr std::string_view data{"i-0e"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), 0);
}

// ----------------------------------------------------------------------------
// ByteString Decoding
// ----------------------------------------------------------------------------

TEST_F(BencodeDecoding, DecodeEmptyByteString) {
  constexpr std::string_view data{"0:"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<ByteString>(result));
  EXPECT_TRUE(std::get<ByteString>(result).empty());
}

TEST_F(BencodeDecoding, DecodeSimpleByteString) {
  constexpr std::string_view data{"5:hello"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<ByteString>(result));
  EXPECT_EQ(ToString(std::get<ByteString>(result)), "hello");
}

TEST_F(BencodeDecoding, DecodeByteStringWithBinaryData) {
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

TEST_F(BencodeDecoding, DecodeLargeByteString) {
  std::string const content(1000, 'x');
  std::string const data = "1000:" + content;
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<ByteString>(result));
  auto const& bytes = std::get<ByteString>(result);
  EXPECT_EQ(bytes.size(), 1000);
  EXPECT_TRUE(std::all_of(bytes.begin(), bytes.end(),
                          [](std::byte b) { return b == std::byte{'x'}; }));
}

// ----------------------------------------------------------------------------
// List Decoding
// ----------------------------------------------------------------------------

TEST_F(BencodeDecoding, DecodeEmptyList) {
  constexpr std::string_view data{"le"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<List>(result));
  EXPECT_TRUE(std::get<List>(result).empty());
}

TEST_F(BencodeDecoding, DecodeListWithIntegers) {
  constexpr std::string_view data{"li1ei2ei3ee"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<List>(result));
  auto const& list = std::get<List>(result);
  ASSERT_EQ(list.size(), 3);

  EXPECT_EQ(std::get<Integer>(list[0]), 1);
  EXPECT_EQ(std::get<Integer>(list[1]), 2);
  EXPECT_EQ(std::get<Integer>(list[2]), 3);
}

TEST_F(BencodeDecoding, DecodeListWithMixedTypes) {
  constexpr std::string_view data{"li42e4:spam3:egge"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<List>(result));
  auto const& list = std::get<List>(result);
  ASSERT_EQ(list.size(), 3);

  EXPECT_EQ(std::get<Integer>(list[0]), 42);
  EXPECT_EQ(ToString(std::get<ByteString>(list[1])), "spam");
  EXPECT_EQ(ToString(std::get<ByteString>(list[2])), "egg");
}

TEST_F(BencodeDecoding, DecodeNestedLists) {
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

// ----------------------------------------------------------------------------
// Dictionary Decoding
// ----------------------------------------------------------------------------

TEST_F(BencodeDecoding, DecodeEmptyDictionary) {
  constexpr std::string_view data{"de"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(result));
  EXPECT_TRUE(std::get<Dictionary>(result).empty());
}

TEST_F(BencodeDecoding, DecodeSimpleDictionary) {
  constexpr std::string_view data{"d3:cow3:moo4:spam4:eggse"};
  auto const result = DecodeFromString(data);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(result));
  auto const& dict = std::get<Dictionary>(result);
  ASSERT_EQ(dict.size(), 2);

  auto const cow_it = dict.find("cow");
  ASSERT_NE(cow_it, dict.end());
  EXPECT_EQ(ToString(std::get<ByteString>(cow_it->second)), "moo");

  auto const spam_it = dict.find("spam");
  ASSERT_NE(spam_it, dict.end());
  EXPECT_EQ(ToString(std::get<ByteString>(spam_it->second)), "eggs");
}

TEST_F(BencodeDecoding, DecodeDictionaryWithMixedValues) {
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
  EXPECT_EQ(ToString(std::get<ByteString>(name_it->second)), "Alice");
}

TEST_F(BencodeDecoding, DecodeNestedDictionary) {
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

// ----------------------------------------------------------------------------
// Complex Structure Decoding
// ----------------------------------------------------------------------------

TEST_F(BencodeDecoding, DecodeComplexTorrentLikeStructure) {
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

// ----------------------------------------------------------------------------
// DecodeFromFile Tests
// ----------------------------------------------------------------------------

TEST_F(BencodeDecoding, DecodeFromFile) {
  std::string const content{"d4:name5:test14:sizei12345ee"};
  auto const file_path = CreateTestFile(content);

  auto const result = DecodeFromFile(file_path);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(result));
  auto const& dict = std::get<Dictionary>(result);
  EXPECT_EQ(dict.size(), 2);
  EXPECT_EQ(std::get<Integer>(dict.at("size")), 12345);
}

TEST_F(BencodeDecoding, NonExistentFile) {
  EXPECT_THROW(DecodeFromFile("/non/existent/file.bencode"),
               std::runtime_error);
}

// ----------------------------------------------------------------------------
// DecodeFromSpan Tests
// ----------------------------------------------------------------------------

TEST_F(BencodeDecoding, DecodeIntegerFromSpan) {
  std::string data{"i42e"};
  std::span<char> range{data.data(), data.size()};

  Value result{DecodeFromSpan(range)};

  ASSERT_TRUE(std::holds_alternative<Integer>(result));
  EXPECT_EQ(std::get<Integer>(result), 42);
}

TEST_F(BencodeDecoding, DecodeByteStringFromSpan) {
  std::string data{"4:spam"};
  std::span<char> range{data.data(), data.size()};

  auto const result = DecodeFromSpan(range);

  ASSERT_TRUE(std::holds_alternative<ByteString>(result));
  EXPECT_EQ(ToString(std::get<ByteString>(result)), "spam");
}

TEST_F(BencodeDecoding, DecodeListFromSpan) {
  constexpr std::string_view data{"li42e4:spame"};
  constexpr std::span<char const> range{data.data(), data.size()};

  auto const result = DecodeFromSpan(range);

  ASSERT_TRUE(std::holds_alternative<List>(result));
  auto const& list = std::get<List>(result);
  ASSERT_EQ(list.size(), 2);
  EXPECT_EQ(std::get<Integer>(list[0]), 42);
  EXPECT_EQ(ToString(std::get<ByteString>(list[1])), "spam");
}

TEST_F(BencodeDecoding, ThrowsOnEmptyRangeFromSpan) {
  EXPECT_THROW(DecodeFromSpan({}), std::invalid_argument);
}

// ----------------------------------------------------------------------------
// Decoding Error Handling
// ----------------------------------------------------------------------------

TEST_F(BencodeDecoding, InvalidIntegerFormat) {
  EXPECT_THROW(DecodeFromString("i12x34e"), std::invalid_argument);
}

TEST_F(BencodeDecoding, MissingIntegerEndMarker) {
  EXPECT_THROW(DecodeFromString("i123"), std::runtime_error);
}

TEST_F(BencodeDecoding, InvalidByteStringLength) {
  // Length says 5 but only 3 bytes
  EXPECT_THROW(DecodeFromString("5:abc"sv), std::runtime_error);
}

TEST_F(BencodeDecoding, InvalidByteStringDelimiter) {
  // Using ';' instead of ':'
  EXPECT_THROW(DecodeFromString("3;abc"s), std::invalid_argument);
}

TEST_F(BencodeDecoding, DuplicateKeyInDictionary) {
  EXPECT_THROW(DecodeFromString("d3:key5:value3:key6:value2e"),
               std::invalid_argument);
}

TEST_F(BencodeDecoding, EmptyString) {
  constexpr std::string_view data{""};
  EXPECT_THROW(DecodeFromString(data), std::invalid_argument);
}

// ============================================================================
// ENCODING TESTS
// ============================================================================

// ----------------------------------------------------------------------------
// Integer Encoding
// ----------------------------------------------------------------------------

TEST_F(BencodeEncoding, EncodePositiveInteger) {
  Value const value{Integer{42}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "i42e");
}

TEST_F(BencodeEncoding, EncodeNegativeInteger) {
  Value const value{Integer{-42}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "i-42e");
}

TEST_F(BencodeEncoding, EncodeZeroInteger) {
  Value const value{Integer{0}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "i0e");
}

TEST_F(BencodeEncoding, EncodeLargeInteger) {
  Value const value{Integer{INT64_MAX}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "i9223372036854775807e");
}

// ----------------------------------------------------------------------------
// ByteString Encoding
// ----------------------------------------------------------------------------

TEST_F(BencodeEncoding, EncodeEmptyByteString) {
  Value const value{ByteString{}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "0:");
}

TEST_F(BencodeEncoding, EncodeSimpleByteString) {
  Value const value{ToByteString("hello")};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "5:hello");
}

TEST_F(BencodeEncoding, EncodeByteStringWithBinaryData) {
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

TEST_F(BencodeEncoding, EncodeLargeByteString) {
  ByteString bytes(1000, std::byte{'x'});
  Value const value{bytes};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result.size(), 1005);  // "1000:" + 1000 bytes
  EXPECT_EQ(result.substr(0, 5), "1000:");
}

// ----------------------------------------------------------------------------
// List Encoding
// ----------------------------------------------------------------------------

TEST_F(BencodeEncoding, EncodeEmptyList) {
  Value const value{List{}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "le");
}

TEST_F(BencodeEncoding, EncodeListWithIntegers) {
  List list{Integer{1}, Integer{2}, Integer{3}};
  Value const value{list};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "li1ei2ei3ee");
}

TEST_F(BencodeEncoding, EncodeListWithMixedTypes) {
  List list{Integer{42}, ToByteString("spam"), ToByteString("egg")};
  Value const value{list};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "li42e4:spam3:egge");
}

TEST_F(BencodeEncoding, EncodeNestedLists) {
  List inner1{Integer{1}, Integer{2}};
  List inner2{Integer{3}, Integer{4}};
  List outer{inner1, inner2};
  Value const value{outer};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "lli1ei2eeli3ei4eee");
}

// ----------------------------------------------------------------------------
// Dictionary Encoding
// ----------------------------------------------------------------------------

TEST_F(BencodeEncoding, EncodeEmptyDictionary) {
  Value const value{Dictionary{}};
  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "de");
}

TEST_F(BencodeEncoding, EncodeSimpleDictionary) {
  Dictionary dict{};
  dict["cow"] = ToByteString("moo");
  dict["spam"] = ToByteString("eggs");
  Value const value{dict};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "d3:cow3:moo4:spam4:eggse");
}

TEST_F(BencodeEncoding, EncodeDictionaryWithMixedValues) {
  Dictionary dict{};
  dict["age"] = Integer{25};
  dict["name"] = ToByteString("Alice");
  Value const value{dict};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "d3:agei25e4:name5:Alicee");
}

TEST_F(BencodeEncoding, EncodeNestedDictionary) {
  Dictionary inner{};
  inner["age"] = Integer{30};
  inner["name"] = ToByteString("Bob");

  Dictionary outer{};
  outer["user"] = inner;
  Value const value{outer};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "d4:userd3:agei30e4:name3:Bobee");
}

TEST_F(BencodeEncoding, EncodeDictionaryKeysAreSorted) {
  Dictionary dict{};
  dict["zebra"] = Integer{1};
  dict["apple"] = Integer{2};
  dict["mango"] = Integer{3};
  Value const value{dict};

  auto const result = EncodeIntoString(value);
  // std::map automatically sorts keys
  EXPECT_EQ(result, "d5:applei2e5:mangoi3e5:zebrai1ee");
}

TEST_F(BencodeEncoding, EncodeEmptyDictionaryKey) {
  Dictionary dict{};
  dict[""] = ToByteString("value");
  Value const value{dict};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(result, "d0:5:valuee");
}

// ----------------------------------------------------------------------------
// Complex Structure Encoding
// ----------------------------------------------------------------------------

TEST_F(BencodeEncoding, EncodeComplexTorrentLikeStructure) {
  Dictionary info{};
  info["length"] = Integer{1024};
  info["name"] = ToByteString("test.txt");
  info["piece length"] = Integer{16384};
  info["pieces"] = ToByteString("01234567890123456789");

  Dictionary torrent{};
  torrent["announce"] = ToByteString("localhost");
  torrent["info"] = info;
  Value const value{torrent};

  auto const result = EncodeIntoString(value);
  EXPECT_EQ(
      result,
      "d8:announce9:localhost4:infod6:lengthi1024e4:name8:test.txt12:piece "
      "lengthi16384e6:pieces20:01234567890123456789ee");
}

// ----------------------------------------------------------------------------
// EncodeIntoFile Tests
// ----------------------------------------------------------------------------

TEST_F(BencodeEncoding, EncodeIntoFile) {
  Dictionary dict{};
  dict["name"] = ToByteString("test1");
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

TEST_F(BencodeEncoding, EncodeIntoNonExistentDirectory) {
  Value const value{Integer{42}};
  auto const bad_path = std::filesystem::path{"/non/existent/dir/file.bencode"};
  EXPECT_THROW(EncodeIntoFile(value, bad_path), std::runtime_error);
}

// ----------------------------------------------------------------------------
// EncodeIntoSpan Tests
// ----------------------------------------------------------------------------

TEST_F(BencodeEncoding, EncodeIntoSpan) {
  Value const value{Integer{42}};

  std::array<char, 10> buffer{};
  std::span<char> span{buffer};

  EncodeIntoSpan(value, span);

  std::string_view result{buffer.data(), 4};
  EXPECT_EQ(result, "i42e");
}

// ----------------------------------------------------------------------------
// Round-Trip Tests
// ----------------------------------------------------------------------------

TEST_F(BencodeEncoding, RoundTripInteger) {
  Value const original{Integer{12345}};
  auto const encoded = EncodeIntoString(original);
  auto const decoded = DecodeFromString(encoded);

  ASSERT_TRUE(std::holds_alternative<Integer>(decoded));
  EXPECT_EQ(std::get<Integer>(decoded), std::get<Integer>(original));
}

TEST_F(BencodeEncoding, RoundTripByteString) {
  Value const original{ToByteString("hello world")};

  auto const encoded = EncodeIntoString(original);
  auto const decoded = DecodeFromString(encoded);

  ASSERT_TRUE(std::holds_alternative<ByteString>(decoded));
  EXPECT_EQ(std::get<ByteString>(decoded), std::get<ByteString>(original));
}

TEST_F(BencodeEncoding, RoundTripList) {
  List list{Integer{1}, Integer{2}, Integer{3}};
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

TEST_F(BencodeEncoding, RoundTripDictionary) {
  Dictionary dict{};
  dict["key"] = ToByteString("value");
  dict["num"] = Integer{999};
  Value const original{dict};

  auto const encoded = EncodeIntoString(original);
  auto const decoded = DecodeFromString(encoded);

  ASSERT_TRUE(std::holds_alternative<Dictionary>(decoded));
  auto const& result_dict = std::get<Dictionary>(decoded);
  EXPECT_EQ(result_dict.size(), 2);
  EXPECT_EQ(std::get<Integer>(result_dict.at("num")), 999);
}

TEST_F(BencodeEncoding, RoundTripComplexStructure) {
  List inner_list{Integer{1}, ToByteString("test")};

  Dictionary inner_dict{};
  inner_dict["x"] = Integer{100};

  List outer_list{inner_list, inner_dict};

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

// ----------------------------------------------------------------------------
// Encoding Error Handling
// ----------------------------------------------------------------------------

TEST_F(BencodeEncoding, EncodeMonostateThrows) {
  Value const value{std::monostate{}};
  EXPECT_THROW(EncodeIntoString(value), std::invalid_argument);
}
