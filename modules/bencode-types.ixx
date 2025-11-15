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
}  // namespace byte_torrent::bencode::meta
}  // namespace byte_torrent::bencode
