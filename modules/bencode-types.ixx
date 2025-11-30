export module bencode:types;

import std;

namespace byte_torrent::bencode {
export struct Value;

export using Integer = std::int64_t;
export using ByteString = std::vector<std::byte>;
export using List = std::vector<Value>;
export using Dictionary = std::map<std::string, Value>;

export struct Value
    : std::variant<std::monostate, Integer, ByteString, List, Dictionary> {
  using variant::variant;
};

// ============================================================================
// ByteString Conversion Utilities
// ============================================================================

// Converts ByteString to std::string (reinterprets bytes as chars)
export [[nodiscard]] inline std::string ToString(ByteString const& bytes) {
  return {reinterpret_cast<char const*>(bytes.data()), bytes.size()};
}

// Converts string_view to ByteString
export [[nodiscard]] inline ByteString ToByteString(std::string_view str) {
  ByteString result(str.size());
  std::memcpy(result.data(), str.data(), str.size());
  return result;
}

// Converts span of bytes to ByteString (copies data)
export [[nodiscard]] inline ByteString ToByteString(
    std::span<std::byte const> bytes) {
  return {bytes.begin(), bytes.end()};
}

// ============================================================================
// Bencode Format Markers
// ============================================================================

namespace meta {
template <std::integral T = int>
constexpr T kIntegerMarker = 'i';
template <std::integral T = int>
constexpr T kListMarker = 'l';
template <std::integral T = int>
constexpr T kDictionaryMarker = 'd';
template <std::integral T = int>
constexpr T kEndMarker = 'e';
template <std::integral T = int>
constexpr T kDelimiter = ':';
}  // namespace meta
}  // namespace byte_torrent::bencode
