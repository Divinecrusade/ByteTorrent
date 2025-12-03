#include <gtest/gtest.h>
import tracker;
import bencode;
import peer_wire;
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace byte_torrent::tracker;
using namespace byte_torrent::bencode;
using namespace byte_torrent::peer_wire;

// ============================================================================
// URL Encoding Tests
// ============================================================================

class UrlEncodingTest : public ::testing::Test {};

TEST_F(UrlEncodingTest, EncodeUnreservedCharacters) {
  // Unreserved chars should pass through unchanged
  EXPECT_EQ(UrlEncode("abc123"), "abc123");
  EXPECT_EQ(UrlEncode("ABC-_.~"), "ABC-_.~");
}

TEST_F(UrlEncodingTest, EncodeReservedCharacters) {
  EXPECT_EQ(UrlEncode(" "), "%20");
  EXPECT_EQ(UrlEncode("&"), "%26");
  EXPECT_EQ(UrlEncode("="), "%3D");
  EXPECT_EQ(UrlEncode("?"), "%3F");
  EXPECT_EQ(UrlEncode("/"), "%2F");
}

TEST_F(UrlEncodingTest, EncodeMixedString) {
  EXPECT_EQ(UrlEncode("hello world"), "hello%20world");
  EXPECT_EQ(UrlEncode("a=b&c=d"), "a%3Db%26c%3Dd");
}

TEST_F(UrlEncodingTest, EncodeByteSpan) {
  std::array<std::byte, 4> bytes{std::byte{0x12}, std::byte{0x34},
                                  std::byte{0xAB}, std::byte{0xCD}};
  auto const encoded = UrlEncode(std::span{bytes});
  EXPECT_EQ(encoded, "%124%AB%CD");
}

TEST_F(UrlEncodingTest, EncodeInfoHashLikeData) {
  // 20 bytes like an info_hash
  std::array<std::byte, 20> hash{};
  for (std::size_t i = 0; i < hash.size(); ++i) {
    hash[i] = static_cast<std::byte>(i * 10);
  }

  auto const encoded = UrlEncode(std::span{hash});
  // First few bytes: 0x00, 0x0A, 0x14, 0x1E, 0x28...
  EXPECT_TRUE(encoded.starts_with("%00%0A%14%1E"));
}

TEST_F(UrlEncodingTest, DecodeSimple) {
  EXPECT_EQ(UrlDecode("hello%20world"), "hello world");
  EXPECT_EQ(UrlDecode("a%3Db"), "a=b");
}

TEST_F(UrlEncodingTest, DecodePassthrough) {
  EXPECT_EQ(UrlDecode("abc123"), "abc123");
}

TEST_F(UrlEncodingTest, DecodeInvalidSequence) {
  // Invalid hex should pass through
  EXPECT_EQ(UrlDecode("abc%GGdef"), "abc%GGdef");
  EXPECT_EQ(UrlDecode("abc%2"), "abc%2");
}

TEST_F(UrlEncodingTest, RoundTrip) {
  std::string const original{"hello=world&foo=bar baz"};
  EXPECT_EQ(UrlDecode(UrlEncode(original)), original);
}

// ============================================================================
// URL Parsing Tests
// ============================================================================

class UrlParsingTest : public ::testing::Test {};

TEST_F(UrlParsingTest, ParseSimpleHttp) {
  auto const url = ParseUrl("http://example.com/announce");
  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url->scheme, "http");
  EXPECT_EQ(url->host, "example.com");
  EXPECT_EQ(url->port, 80);
  EXPECT_EQ(url->path, "/announce");
  EXPECT_TRUE(url->query.empty());
}

TEST_F(UrlParsingTest, ParseHttpWithPort) {
  auto const url = ParseUrl("http://tracker.example.com:6969/announce");
  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url->host, "tracker.example.com");
  EXPECT_EQ(url->port, 6969);
  EXPECT_EQ(url->path, "/announce");
}

TEST_F(UrlParsingTest, ParseHttps) {
  auto const url = ParseUrl("https://secure.tracker.com/announce");
  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url->scheme, "https");
  EXPECT_EQ(url->port, 443);
}

TEST_F(UrlParsingTest, ParseWithQuery) {
  auto const url = ParseUrl("http://example.com/announce?passkey=abc123");
  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url->path, "/announce");
  EXPECT_EQ(url->query, "passkey=abc123");
}

TEST_F(UrlParsingTest, ParseWithComplexPath) {
  auto const url = ParseUrl("http://example.com:8080/tracker/announce.php?key=val");
  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url->port, 8080);
  EXPECT_EQ(url->path, "/tracker/announce.php");
  EXPECT_EQ(url->query, "key=val");
}

TEST_F(UrlParsingTest, ParseNoPath) {
  auto const url = ParseUrl("http://example.com");
  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url->path, "/");
}

TEST_F(UrlParsingTest, ParseIPv4) {
  auto const url = ParseUrl("http://192.168.1.1:6969/announce");
  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url->host, "192.168.1.1");
  EXPECT_EQ(url->port, 6969);
}

TEST_F(UrlParsingTest, ParseIPv6) {
  auto const url = ParseUrl("http://[::1]:6969/announce");
  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url->host, "[::1]");
  EXPECT_EQ(url->port, 6969);
}

TEST_F(UrlParsingTest, ParseInvalidNoScheme) {
  auto const url = ParseUrl("example.com/announce");
  EXPECT_FALSE(url.has_value());
}

TEST_F(UrlParsingTest, ToString) {
  Url url{.scheme = "http",
          .host = "tracker.example.com",
          .port = 6969,
          .path = "/announce",
          .query = "key=value"};
  EXPECT_EQ(url.ToString(), "http://tracker.example.com:6969/announce?key=value");
}

TEST_F(UrlParsingTest, ToStringDefaultPort) {
  Url url{.scheme = "http", .host = "example.com", .port = 80, .path = "/path"};
  EXPECT_EQ(url.ToString(), "http://example.com/path");
}

// ============================================================================
// Query Builder Tests
// ============================================================================

class QueryBuilderTest : public ::testing::Test {};

TEST_F(QueryBuilderTest, BuildEmpty) {
  QueryBuilder builder{};
  EXPECT_TRUE(builder.Build().empty());
}

TEST_F(QueryBuilderTest, BuildSingleParam) {
  QueryBuilder builder{};
  builder.Add("key", "value");
  EXPECT_EQ(builder.Build(), "key=value");
}

TEST_F(QueryBuilderTest, BuildMultipleParams) {
  QueryBuilder builder{};
  builder.Add("a", "1").Add("b", "2").Add("c", "3");
  EXPECT_EQ(builder.Build(), "a=1&b=2&c=3");
}

TEST_F(QueryBuilderTest, BuildWithEncoding) {
  QueryBuilder builder{};
  builder.Add("message", "hello world");
  EXPECT_EQ(builder.Build(), "message=hello%20world");
}

TEST_F(QueryBuilderTest, BuildWithInteger) {
  QueryBuilder builder{};
  builder.Add("port", 6881).Add("uploaded", 1024ULL);
  EXPECT_EQ(builder.Build(), "port=6881&uploaded=1024");
}

TEST_F(QueryBuilderTest, BuildWithOptional) {
  QueryBuilder builder{};
  builder.AddOptional("present", std::optional<int>{42})
      .AddOptional("absent", std::optional<int>{});
  EXPECT_EQ(builder.Build(), "present=42");
}

TEST_F(QueryBuilderTest, BuildWithConditional) {
  QueryBuilder builder{};
  builder.AddIf(true, "yes", "1").AddIf(false, "no", "0");
  EXPECT_EQ(builder.Build(), "yes=1");
}

// ============================================================================
// Announce Query Building Tests
// ============================================================================

class AnnounceQueryTest : public ::testing::Test {
 protected:
  static AnnounceRequest MakeRequest() {
    AnnounceRequest req{};
    // Set info_hash
    for (std::size_t i = 0; i < req.info_hash.size(); ++i) {
      req.info_hash[i] = static_cast<std::byte>(i);
    }
    // Set peer_id
    std::string_view const prefix{"-BT0001-"};
    for (std::size_t i = 0; i < prefix.size(); ++i) {
      req.peer_id[i] = static_cast<std::byte>(prefix[i]);
    }
    for (std::size_t i = prefix.size(); i < req.peer_id.size(); ++i) {
      req.peer_id[i] = static_cast<std::byte>('A' + i - prefix.size());
    }
    req.port = 6881;
    req.uploaded = 0;
    req.downloaded = 1000;
    req.left = 500000;
    return req;
  }
};

TEST_F(AnnounceQueryTest, BuildBasicQuery) {
  auto const req = MakeRequest();
  auto const query = BuildAnnounceQuery(req);

  EXPECT_TRUE(query.find("info_hash=") != std::string::npos);
  EXPECT_TRUE(query.find("peer_id=") != std::string::npos);
  EXPECT_TRUE(query.find("port=6881") != std::string::npos);
  EXPECT_TRUE(query.find("uploaded=0") != std::string::npos);
  EXPECT_TRUE(query.find("downloaded=1000") != std::string::npos);
  EXPECT_TRUE(query.find("left=500000") != std::string::npos);
  EXPECT_TRUE(query.find("compact=1") != std::string::npos);
}

TEST_F(AnnounceQueryTest, BuildQueryWithEvent) {
  auto req = MakeRequest();
  req.event = TrackerEvent::Started;

  auto const query = BuildAnnounceQuery(req);
  EXPECT_TRUE(query.find("event=started") != std::string::npos);
}

TEST_F(AnnounceQueryTest, BuildQueryNoEventForNone) {
  auto req = MakeRequest();
  req.event = TrackerEvent::None;

  auto const query = BuildAnnounceQuery(req);
  EXPECT_TRUE(query.find("event=") == std::string::npos);
}

TEST_F(AnnounceQueryTest, BuildAnnounceUrl) {
  auto const req = MakeRequest();
  auto const url = BuildAnnounceUrl("http://tracker.example.com/announce", req);

  EXPECT_EQ(url.scheme, "http");
  EXPECT_EQ(url.host, "tracker.example.com");
  EXPECT_EQ(url.path, "/announce");
  EXPECT_FALSE(url.query.empty());
}

TEST_F(AnnounceQueryTest, BuildAnnounceUrlWithExistingQuery) {
  auto const req = MakeRequest();
  auto const url =
      BuildAnnounceUrl("http://tracker.example.com/announce?passkey=secret", req);

  EXPECT_TRUE(url.query.starts_with("passkey=secret&"));
}

// ============================================================================
// Scrape URL Tests
// ============================================================================

TEST(ScrapeUrlTest, GetScrapeUrlValid) {
  auto const scrape = GetScrapeUrl("http://tracker.com/announce");
  ASSERT_TRUE(scrape.has_value());
  EXPECT_EQ(*scrape, "http://tracker.com/scrape");
}

TEST(ScrapeUrlTest, GetScrapeUrlWithPath) {
  auto const scrape = GetScrapeUrl("http://tracker.com/path/announce");
  ASSERT_TRUE(scrape.has_value());
  EXPECT_EQ(*scrape, "http://tracker.com/path/scrape");
}

TEST(ScrapeUrlTest, GetScrapeUrlWithQuery) {
  auto const scrape = GetScrapeUrl("http://tracker.com/announce?key=value");
  ASSERT_TRUE(scrape.has_value());
  EXPECT_EQ(*scrape, "http://tracker.com/scrape?key=value");
}

TEST(ScrapeUrlTest, GetScrapeUrlInvalid) {
  auto const scrape = GetScrapeUrl("http://tracker.com/track");
  EXPECT_FALSE(scrape.has_value());
}

// ============================================================================
// Compact Peer Parsing Tests
// ============================================================================

class CompactPeerParsingTest : public ::testing::Test {};

TEST_F(CompactPeerParsingTest, ParseSinglePeer) {
  // IP: 192.168.1.1, Port: 6881 (0x1AE1)
  std::array<std::byte, 6> data{std::byte{192}, std::byte{168}, std::byte{1},
                                std::byte{1},   std::byte{0x1A}, std::byte{0xE1}};

  auto const peers = ParseCompactPeers(data);

  ASSERT_EQ(peers.size(), 1u);
  EXPECT_EQ(peers[0].ip, "192.168.1.1");
  EXPECT_EQ(peers[0].port, 6881);
}

TEST_F(CompactPeerParsingTest, ParseMultiplePeers) {
  std::vector<std::byte> data{// Peer 1: 10.0.0.1:6881
                              std::byte{10}, std::byte{0}, std::byte{0},
                              std::byte{1}, std::byte{0x1A}, std::byte{0xE1},
                              // Peer 2: 10.0.0.2:51413
                              std::byte{10}, std::byte{0}, std::byte{0},
                              std::byte{2}, std::byte{0xC8}, std::byte{0xD5}};

  auto const peers = ParseCompactPeers(data);

  ASSERT_EQ(peers.size(), 2u);
  EXPECT_EQ(peers[0].ip, "10.0.0.1");
  EXPECT_EQ(peers[0].port, 6881);
  EXPECT_EQ(peers[1].ip, "10.0.0.2");
  EXPECT_EQ(peers[1].port, 51413);
}

TEST_F(CompactPeerParsingTest, ParseEmptyData) {
  auto const peers = ParseCompactPeers({});
  EXPECT_TRUE(peers.empty());
}

TEST_F(CompactPeerParsingTest, ThrowOnInvalidSize) {
  std::array<std::byte, 5> invalid{};  // Not multiple of 6
  EXPECT_THROW(ParseCompactPeers(invalid), std::invalid_argument);
}

TEST_F(CompactPeerParsingTest, ParseIPv6Peers) {
  // ::1:6881
  std::array<std::byte, 18> data{};
  data[14] = std::byte{0};
  data[15] = std::byte{1};  // ::1
  data[16] = std::byte{0x1A};
  data[17] = std::byte{0xE1};  // Port 6881

  auto const peers = ParseCompactPeers6(data);

  ASSERT_EQ(peers.size(), 1u);
  EXPECT_EQ(peers[0].port, 6881);
  // IPv6 format
  EXPECT_TRUE(peers[0].ip.starts_with("["));
  EXPECT_TRUE(peers[0].ip.ends_with("]"));
}

// ============================================================================
// Announce Response Parsing Tests
// ============================================================================

class AnnounceResponseParsingTest : public ::testing::Test {
 protected:
  static std::string MakeCompactPeerData() {
    // 2 peers in compact format
    return std::string{
        static_cast<char>(192), static_cast<char>(168), static_cast<char>(1),
        static_cast<char>(1),   static_cast<char>(0x1A), static_cast<char>(0xE1),
        static_cast<char>(10),  static_cast<char>(0),   static_cast<char>(0),
        static_cast<char>(1),   static_cast<char>(0x1A), static_cast<char>(0xE1)};
  }
};

TEST_F(AnnounceResponseParsingTest, ParseSuccessfulResponse) {
  Dictionary response{};
  response["interval"] = Integer{1800};
  response["complete"] = Integer{10};
  response["incomplete"] = Integer{5};
  response["peers"] = ToByteString(MakeCompactPeerData());

  auto const encoded = EncodeIntoString(Value{response});
  auto const result = ParseAnnounceResponse(encoded);

  ASSERT_TRUE(result.IsOk());
  EXPECT_EQ(result->interval.count(), 1800);
  EXPECT_EQ(result->complete, 10u);
  EXPECT_EQ(result->incomplete, 5u);
  EXPECT_EQ(result->peers.size(), 2u);
}

TEST_F(AnnounceResponseParsingTest, ParseWithMinInterval) {
  Dictionary response{};
  response["interval"] = Integer{1800};
  response["min interval"] = Integer{900};
  response["peers"] = ToByteString("");

  auto const encoded = EncodeIntoString(Value{response});
  auto const result = ParseAnnounceResponse(encoded);

  ASSERT_TRUE(result.IsOk());
  ASSERT_TRUE(result->min_interval.has_value());
  EXPECT_EQ(result->min_interval->count(), 900);
}

TEST_F(AnnounceResponseParsingTest, ParseWithTrackerId) {
  Dictionary response{};
  response["interval"] = Integer{1800};
  response["tracker id"] = ToByteString("session-12345");
  response["peers"] = ToByteString("");

  auto const encoded = EncodeIntoString(Value{response});
  auto const result = ParseAnnounceResponse(encoded);

  ASSERT_TRUE(result.IsOk());
  ASSERT_TRUE(result->tracker_id.has_value());
  EXPECT_EQ(*result->tracker_id, "session-12345");
}

TEST_F(AnnounceResponseParsingTest, ParseWithWarning) {
  Dictionary response{};
  response["interval"] = Integer{1800};
  response["warning message"] = ToByteString("Rate limit exceeded");
  response["peers"] = ToByteString("");

  auto const encoded = EncodeIntoString(Value{response});
  auto const result = ParseAnnounceResponse(encoded);

  ASSERT_TRUE(result.IsOk());
  ASSERT_TRUE(result->warning_message.has_value());
  EXPECT_EQ(*result->warning_message, "Rate limit exceeded");
}

TEST_F(AnnounceResponseParsingTest, ParseDictionaryPeerFormat) {
  List peer_list{};

  Dictionary peer1{};
  peer1["ip"] = ToByteString("192.168.1.1");
  peer1["port"] = Integer{6881};
  peer_list.push_back(peer1);

  Dictionary peer2{};
  peer2["ip"] = ToByteString("10.0.0.1");
  peer2["port"] = Integer{51413};
  peer_list.push_back(peer2);

  Dictionary response{};
  response["interval"] = Integer{1800};
  response["peers"] = peer_list;

  auto const encoded = EncodeIntoString(Value{response});
  auto const result = ParseAnnounceResponse(encoded);

  ASSERT_TRUE(result.IsOk());
  ASSERT_EQ(result->peers.size(), 2u);
  EXPECT_EQ(result->peers[0].ip, "192.168.1.1");
  EXPECT_EQ(result->peers[0].port, 6881);
  EXPECT_EQ(result->peers[1].ip, "10.0.0.1");
  EXPECT_EQ(result->peers[1].port, 51413);
}

TEST_F(AnnounceResponseParsingTest, ParseFailureReason) {
  Dictionary response{};
  response["failure reason"] = ToByteString("Invalid info_hash");

  auto const encoded = EncodeIntoString(Value{response});
  auto const result = ParseAnnounceResponse(encoded);

  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.error.code, TrackerErrorCode::TrackerError);
  EXPECT_EQ(result.error.message, "Invalid info_hash");
}

TEST_F(AnnounceResponseParsingTest, ParseMissingInterval) {
  Dictionary response{};
  response["peers"] = ToByteString("");

  auto const encoded = EncodeIntoString(Value{response});
  auto const result = ParseAnnounceResponse(encoded);

  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.error.code, TrackerErrorCode::InvalidResponse);
}

TEST_F(AnnounceResponseParsingTest, ParseInvalidBencode) {
  auto const result = ParseAnnounceResponse("not valid bencode{{{");
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.error.code, TrackerErrorCode::ParseError);
}

TEST_F(AnnounceResponseParsingTest, ParseNonDictionary) {
  auto const encoded = EncodeIntoString(Value{Integer{42}});
  auto const result = ParseAnnounceResponse(encoded);

  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.error.code, TrackerErrorCode::InvalidResponse);
}

// ============================================================================
// HTTP Response Parser Tests
// ============================================================================

class HttpResponseParserTest : public ::testing::Test {
 protected:
  static std::vector<std::byte> ToBytes(std::string_view str) {
    std::vector<std::byte> result(str.size());
    std::transform(str.begin(), str.end(), result.begin(),
                   [](char c) { return static_cast<std::byte>(c); });
    return result;
  }
};

TEST_F(HttpResponseParserTest, ParseSimpleResponse) {
  std::string_view const response =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 5\r\n"
      "\r\n"
      "hello";

  HttpResponseParser parser{};
  parser.Feed(ToBytes(response));

  ASSERT_TRUE(parser.IsComplete());
  auto const result = parser.TakeResponse();
  EXPECT_EQ(result.status_code, 200);
  EXPECT_EQ(result.BodyAsString(), "hello");
}

TEST_F(HttpResponseParserTest, ParseHeaders) {
  std::string_view const response =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/octet-stream\r\n"
      "X-Custom-Header: custom-value\r\n"
      "Content-Length: 0\r\n"
      "\r\n";

  HttpResponseParser parser{};
  parser.Feed(ToBytes(response));

  ASSERT_EQ(parser.GetState(), HttpResponseParser::State::Body);
  auto const result = parser.TakeResponse();

  auto const content_type = result.GetHeader("Content-Type");
  ASSERT_TRUE(content_type.has_value());
  EXPECT_EQ(*content_type, "application/octet-stream");

  auto const custom = result.GetHeader("X-Custom-Header");
  ASSERT_TRUE(custom.has_value());
  EXPECT_EQ(*custom, "custom-value");
}

TEST_F(HttpResponseParserTest, ParseStatusCodes) {
  auto testStatus = [this](int code, std::string_view phrase) {
    std::string response =
        "HTTP/1.1 " + std::to_string(code) + " " + std::string{phrase} +
        "\r\nContent-Length: 0\r\n\r\n";

    HttpResponseParser parser{};
    parser.Feed(ToBytes(response));

    EXPECT_EQ(parser.TakeResponse().status_code, code);
  };

  testStatus(200, "OK");
  testStatus(301, "Moved Permanently");
  testStatus(404, "Not Found");
  testStatus(500, "Internal Server Error");
}

TEST_F(HttpResponseParserTest, ParseChunkedResponse) {
  std::string_view const response =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "5\r\n"
      "hello\r\n"
      "6\r\n"
      " world\r\n"
      "0\r\n"
      "\r\n";

  HttpResponseParser parser{};
  parser.Feed(ToBytes(response));

  ASSERT_TRUE(parser.IsComplete());
  auto const result = parser.TakeResponse();
  EXPECT_EQ(result.BodyAsString(), "hello world");
}

TEST_F(HttpResponseParserTest, ParseIncrementalFeed) {
  std::string_view const part1 = "HTTP/1.1 200 OK\r\n";
  std::string_view const part2 = "Content-Length: 4\r\n\r\n";
  std::string_view const part3 = "test";

  HttpResponseParser parser{};

  parser.Feed(ToBytes(part1));
  EXPECT_FALSE(parser.IsComplete());

  parser.Feed(ToBytes(part2));
  EXPECT_FALSE(parser.IsComplete());

  parser.Feed(ToBytes(part3));
  ASSERT_TRUE(parser.IsComplete());

  EXPECT_EQ(parser.TakeResponse().BodyAsString(), "test");
}

TEST_F(HttpResponseParserTest, IsSuccessAndRedirect) {
  HttpResponse success{.status_code = 200};
  HttpResponse redirect{.status_code = 301};
  HttpResponse error{.status_code = 404};

  EXPECT_TRUE(success.IsSuccess());
  EXPECT_FALSE(success.IsRedirect());

  EXPECT_FALSE(redirect.IsSuccess());
  EXPECT_TRUE(redirect.IsRedirect());

  EXPECT_FALSE(error.IsSuccess());
  EXPECT_FALSE(error.IsRedirect());
}

// ============================================================================
// HTTP Request Builder Tests
// ============================================================================

TEST(HttpRequestBuilderTest, BuildSimpleRequest) {
  Url url{.scheme = "http",
          .host = "tracker.example.com",
          .port = 80,
          .path = "/announce",
          .query = "info_hash=test"};

  auto const request = BuildHttpRequest(url);

  EXPECT_TRUE(request.starts_with("GET /announce?info_hash=test HTTP/1.1\r\n"));
  EXPECT_TRUE(request.find("Host: tracker.example.com\r\n") != std::string::npos);
  EXPECT_TRUE(request.find("User-Agent: ByteTorrent") != std::string::npos);
  EXPECT_TRUE(request.ends_with("\r\n\r\n"));
}

TEST(HttpRequestBuilderTest, BuildRequestWithNonDefaultPort) {
  Url url{.scheme = "http",
          .host = "tracker.example.com",
          .port = 6969,
          .path = "/announce"};

  auto const request = BuildHttpRequest(url);

  EXPECT_TRUE(request.find("Host: tracker.example.com:6969\r\n") !=
              std::string::npos);
}

// ============================================================================
// TrackerEvent Tests
// ============================================================================

TEST(TrackerEventTest, ToString) {
  EXPECT_EQ(ToString(TrackerEvent::None), "");
  EXPECT_EQ(ToString(TrackerEvent::Started), "started");
  EXPECT_EQ(ToString(TrackerEvent::Stopped), "stopped");
  EXPECT_EQ(ToString(TrackerEvent::Completed), "completed");
}

// ============================================================================
// TrackerError Tests
// ============================================================================

TEST(TrackerErrorTest, ErrorCodeToString) {
  EXPECT_EQ(ToString(TrackerErrorCode::None), "None");
  EXPECT_EQ(ToString(TrackerErrorCode::NetworkError), "NetworkError");
  EXPECT_EQ(ToString(TrackerErrorCode::Timeout), "Timeout");
  EXPECT_EQ(ToString(TrackerErrorCode::TrackerError), "TrackerError");
}

TEST(TrackerErrorTest, IsOk) {
  TrackerError ok{.code = TrackerErrorCode::None};
  TrackerError err{.code = TrackerErrorCode::NetworkError};

  EXPECT_TRUE(ok.IsOk());
  EXPECT_FALSE(err.IsOk());
}

// ============================================================================
// TrackerResult Tests
// ============================================================================

TEST(TrackerResultTest, SuccessResult) {
  auto result = MakeSuccess<int>(42);

  EXPECT_TRUE(result.IsOk());
  EXPECT_TRUE(static_cast<bool>(result));
  EXPECT_EQ(*result, 42);
}

TEST(TrackerResultTest, ErrorResult) {
  auto result = MakeError<int>(TrackerErrorCode::Timeout, "Connection timed out");

  EXPECT_FALSE(result.IsOk());
  EXPECT_FALSE(static_cast<bool>(result));
  EXPECT_EQ(result.error.code, TrackerErrorCode::Timeout);
  EXPECT_EQ(result.error.message, "Connection timed out");
}

// ============================================================================
// AnnounceRequest Tests
// ============================================================================

TEST(AnnounceRequestTest, IsValidDefault) {
  AnnounceRequest req{};
  EXPECT_FALSE(req.IsValid());  // All zeros
}

TEST(AnnounceRequestTest, IsValidWithData) {
  AnnounceRequest req{};
  req.info_hash[0] = std::byte{1};
  req.peer_id[0] = std::byte{1};
  req.port = 6881;

  EXPECT_TRUE(req.IsValid());
}

TEST(AnnounceRequestTest, IsValidNoPort) {
  AnnounceRequest req{};
  req.info_hash[0] = std::byte{1};
  req.peer_id[0] = std::byte{1};
  req.port = 0;

  EXPECT_FALSE(req.IsValid());
}

// ============================================================================
// TrackerClient Tests (without network)
// ============================================================================

TEST(TrackerClientTest, GetScrapeUrl) {
  TrackerClient client{"http://tracker.example.com/announce"};

  auto const scrape = client.GetScrapeUrl();
  ASSERT_TRUE(scrape.has_value());
  EXPECT_EQ(*scrape, "http://tracker.example.com/scrape");
}

TEST(TrackerClientTest, GetAnnounceUrl) {
  TrackerClient client{"http://tracker.example.com/announce"};
  EXPECT_EQ(client.GetAnnounceUrl(), "http://tracker.example.com/announce");
}

TEST(TrackerClientTest, AnnounceWithoutHttpClient) {
  TrackerClient client{"http://tracker.example.com/announce"};
  AnnounceRequest req{};
  req.info_hash[0] = std::byte{1};
  req.peer_id[0] = std::byte{1};
  req.port = 6881;

  auto result = client.Announce(req);

  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.error.code, TrackerErrorCode::NetworkError);
}

// ============================================================================
// MultiTrackerClient Tests
// ============================================================================

TEST(MultiTrackerClientTest, TierCount) {
  std::vector<std::vector<std::string>> tiers{
      {"http://tracker1.com/announce", "http://tracker2.com/announce"},
      {"http://backup.com/announce"}};

  MultiTrackerClient client{tiers};

  EXPECT_EQ(client.TierCount(), 2u);
  EXPECT_EQ(client.TotalTrackerCount(), 3u);
}

TEST(MultiTrackerClientTest, EmptyTiers) {
  MultiTrackerClient client{{}};

  EXPECT_EQ(client.TierCount(), 0u);

  AnnounceRequest req{};
  auto result = client.Announce(req);
  EXPECT_FALSE(result.IsOk());
}
