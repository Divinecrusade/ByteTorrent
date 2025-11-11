export module bencode;

import <cstdint>;
import <span>;
import <vector>;
import <map>;
import <string_view>;
import <variant>;
import <filesystem>;
import <iostream>;
import <fstream>;
import <spanstream>;
import <unordered_map>;
import <functional>;
import <type_traits>;
import <utility>;

namespace byte_torrent::bencode {
export struct Value;

export using Integer = std::int64_t;
export using ByteString = std::vector<std::byte>;
export using List = std::vector<Value>;
export using Dictionary = std::map<std::string,
                                   Value>;

export struct Value : std::variant<std::monostate,
                           Integer, 
                           ByteString,
                           List,
                           Dictionary> {
  using variant::variant;
};

namespace {
enum struct MetaSymbol : int {
  kIntegerMarker = 'i',
  kListMarker = 'l',
  kDictionaryMarker = 'd',
  kEndMarker = 'e',
  kDelimiter = ':',
  kUndefined
}; 
std::istream& operator>>(std::istream& in, MetaSymbol& token) {
  using MetaSymbolType = std::underlying_type_t<MetaSymbol>;
  switch (in.get()) {
    case static_cast<MetaSymbolType>(MetaSymbol::kIntegerMarker):
      token = MetaSymbol::kIntegerMarker;
      break;
    case static_cast<MetaSymbolType>(MetaSymbol::kListMarker):
      token = MetaSymbol::kListMarker;
      break;
    case static_cast<MetaSymbolType>(MetaSymbol::kDictionaryMarker):
      token = MetaSymbol::kDictionaryMarker;
      break;
    case static_cast<MetaSymbolType>(MetaSymbol::kEndMarker):
      token = MetaSymbol::kEndMarker;
      break;
    case static_cast<MetaSymbolType>(MetaSymbol::kDelimiter):
      token = MetaSymbol::kDelimiter;
      break;
    default: 
      token = MetaSymbol::kUndefined;
      in.unget();
  }
  return in;
}

[[nodiscard]] Value Decode(std::istream& src);

[[nodiscard]] Integer DecodeInteger(std::istream& src) {
  bool negative = false;
  if (src.peek() == '-') {
    negative = true;
    std::ignore = src.get();
  }

  if (!std::isdigit(src.peek())) {
    throw std::invalid_argument{"Invalid integer format"};
  }

  Integer result{};
  MetaSymbol token{};
  while (src >> token && token != MetaSymbol::kEndMarker) {
    int const digit = src.get();
    if (!std::isdigit(digit)) {
      throw std::invalid_argument{"Invalid integer format"};
    }
    result = result * 10 + digit;
  }
  if (token != MetaSymbol::kEndMarker) {
    throw std::runtime_error{"Unexpected end of stream"};
  }

  return negative ? -result : result;
}

[[nodiscard]] ByteString DecodeByteString(std::istream& src) {
  std::size_t length{};
  int digit{};
  while (std::isdigit(src.peek())) {
    digit = src.get();
    length = length * 10 + (digit - '0');
  }

  MetaSymbol token{};
  src >> token;
  if (token != MetaSymbol::kDelimiter) {
    throw std::invalid_argument{"Expected ':' delimiter in byte string"};
  }

  ByteString result(length);
  src.read(reinterpret_cast<char*>(result.data()),
           static_cast<std::streamsize>(length));

  if (src.gcount() != static_cast<std::streamsize>(length)) {
    throw std::runtime_error{
        "Unexpected end of stream while reading byte string"};
  }

  return result;
}

[[nodiscard]] List DecodeList(std::istream& src) {
  List result{};
  MetaSymbol token{};
  while (src >> token && token != MetaSymbol::kEndMarker) {
    if (token != MetaSymbol::kUndefined) {
      throw std::invalid_argument{"Unexpected token in list"};
    }
    result.push_back(Decode(src));
  }

  if (token != MetaSymbol::kEndMarker) {
    throw std::runtime_error{"Unexpected end of stream while reading list"};
  }

  return result;
}

[[nodiscard]] Dictionary DecodeDictionary(std::istream& src) {
  Dictionary result{};
  MetaSymbol token{};
  while (src >> token && token != MetaSymbol::kEndMarker) {
    if (token != MetaSymbol::kUndefined) {
      throw std::invalid_argument{"Expected key in dictionary"};
    }

    auto key_bytes = std::get<ByteString>(Decode(src));
    std::string key(reinterpret_cast<char const*>(key_bytes.data()),
                    key_bytes.size());

    if (auto [_, inserted] = result.try_emplace(std::move(key), Decode(src));
        !inserted) {
      throw std::invalid_argument{"Duplicate key in dictionary"};
    }
  }

  if (token != MetaSymbol::kEndMarker) {
    throw std::runtime_error{"Unexpected end of stream while reading dictionary"};
  }

  return result;
}

[[nodiscard]] Value Decode(std::istream& src) {
  if (src.eof())
    throw std::runtime_error{"Unexpected end of stream"};
  if (!src.good())
    throw std::invalid_argument{"Stream is corrupted"};
  
  MetaSymbol token{};
  if (!(src >> token))
    throw std::runtime_error{"Unexpected end of stream"};

  switch (token) {
    case MetaSymbol::kIntegerMarker:
      return DecodeInteger(src);
    case MetaSymbol::kListMarker:
      return DecodeList(src);
    case MetaSymbol::kDictionaryMarker:
      return DecodeDictionary(src);
    case MetaSymbol::kUndefined: [[fallthrough]];
    default:
    {
      if (std::isdigit(src.peek())) {
        return DecodeByteString(src);
      } 
      else
        throw std::invalid_argument{"Invalid bencode format"};
    }
  }
}
}

export [[nodiscard]] Value Decode(std::filesystem::path const& src) {
  if (!std::filesystem::exists(src)) throw std::runtime_error{"File doesn't existed"};
  std::ifstream file{src, std::ios::binary};
  if (!file) throw std::runtime_error{"Cannot open file"};
  return Decode(file);
}

export [[nodiscard]] Value Decode(std::span<char> src) {
  std::ispanstream sin{src, std::ios::binary};
  return Decode(sin);
}
}  // byte_torrent::bencode
