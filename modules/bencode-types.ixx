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
constexpr int kIntegerMarker = 'i';
constexpr int kListMarker = 'l';
constexpr int kDictionaryMarker = 'd';
constexpr int kEndMarker = 'e';
constexpr int kDelimiter = ':';
}  // namespace byte_torrent::bencode::meta
}  // namespace byte_torrent::bencode
