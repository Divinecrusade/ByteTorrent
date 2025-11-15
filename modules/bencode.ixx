export module bencode;

import std;

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
namespace meta {
constexpr int kIntegerMarker = 'i';
constexpr int kListMarker = 'l';
constexpr int kDictionaryMarker = 'd';
constexpr int kEndMarker = 'e';
constexpr int kDelimiter = ':';
}  // namespace meta

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
  while (src.peek() != meta::kEndMarker && !src.eof()) {
    int const digit = src.get();
    if (!std::isdigit(digit)) {
      throw std::invalid_argument{"Invalid integer format"};
    }
    result = result * 10 + (digit - '0');
  }

  if (src.eof() || src.get() != meta::kEndMarker) {
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

  if (src.get() != meta::kDelimiter) {
    throw std::invalid_argument{"Expected ':' delimiter in byte string"};
  }

  ByteString result(length);
  src.read(reinterpret_cast<char*>(result.data()),
           static_cast<std::streamsize>(length));

  if (src.gcount() != static_cast<std::streamsize>(length)) {
    throw std::runtime_error{"Unexpected end of stream while reading byte string"};
  }

  return result;
}

[[nodiscard]] List DecodeList(std::istream& src) {
  List result{};
  while (src.peek() != meta::kEndMarker) {
    result.push_back(Decode(src));
  }

  if (src.get() != meta::kEndMarker) {
    throw std::runtime_error{"Unexpected end of stream while reading list"};
  }

  return result;
}

[[nodiscard]] Dictionary DecodeDictionary(std::istream& src) {
  Dictionary result{};
  while (src.peek() != meta::kEndMarker) {
    auto key_bytes{std::get<ByteString>(Decode(src))};
    if (auto [_, inserted] = result.try_emplace(
            {reinterpret_cast<char const*>(key_bytes.data()), key_bytes.size()},
            Decode(src));
        !inserted) {
      throw std::invalid_argument{"Duplicate key in dictionary"};
    }
  }

  if (src.get() != meta::kEndMarker) {
    throw std::runtime_error{"Unexpected end of stream while reading dictionary"};
  }

  return result;
}

[[nodiscard]] Value Decode(std::istream& src) {
  if (std::ignore = src.peek(); src.eof()) {
    throw std::runtime_error{"Unexpected end of stream"};
  }
  if (!src.good()) {
    throw std::invalid_argument{"Stream is corrupted"};
  }

  switch (int const next_char = src.peek(); 
          next_char) {
    case meta::kIntegerMarker:
      std::ignore = src.get();
      return DecodeInteger(src);
    case meta::kListMarker:
      std::ignore = src.get();
      return DecodeList(src);
    case meta::kDictionaryMarker:
      std::ignore = src.get();
      return DecodeDictionary(src);
    default:
      if (std::isdigit(next_char)) return DecodeByteString(src);
      throw std::invalid_argument{"Invalid bencode format"};
  }
}
}  // namespace

export [[nodiscard]] Value DecodeFromFile(std::filesystem::path const& src) {
  if (!std::filesystem::exists(src)) {
    throw std::runtime_error{"File doesn't exist"};
  }

  std::ifstream file{src, std::ios::binary};

  if (!file) {
    throw std::runtime_error{"Cannot open file"};
  }

  return Decode(file);
}

export [[nodiscard]] Value DecodeFromSpan(std::span<char const> src) {
  if (src.empty()) {
    throw std::invalid_argument{"The range is empty"};
  }
  std::ispanstream sin{src};
  return Decode(sin);
}

export [[nodiscard]] Value DecodeFromString(std::string_view src) {
  if (src.empty()) {
    throw std::invalid_argument{"The string is empty"};
  }
  return DecodeFromSpan({src.data(), src.size()});
}

namespace {
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};

void Encode(Value const& src, std::ostream& dst) {
  std::visit(
      overloaded{[&dst](std::monostate) {
                   throw std::invalid_argument{"Cannot encode monostate"};
                 },
                 [&dst](Integer value) {
                   dst << static_cast<char>(meta::kIntegerMarker) << value
                       << static_cast<char>(meta::kEndMarker);
                 },
                 [&dst](ByteString const& value) {
                   dst << value.size() << static_cast<char>(meta::kDelimiter);
                   dst.write(reinterpret_cast<char const*>(value.data()),
                             static_cast<std::streamsize>(value.size()));
                 },
                 [&dst](List const& value) {
                   dst << static_cast<char>(meta::kListMarker);
                   for (auto const& element : value) {
                     Encode(element, dst);
                   }
                   dst << static_cast<char>(meta::kEndMarker);
                 },
                 [&dst](Dictionary const& value) {
                   dst << static_cast<char>(meta::kDictionaryMarker);
                   for (auto const& [key, val] : value) {
                     dst << key.size() << static_cast<char>(meta::kDelimiter);
                     dst.write(key.data(),
                               static_cast<std::streamsize>(key.size()));
                     Encode(val, dst);
                   }
                   dst << static_cast<char>(meta::kEndMarker);
                 }},
      src);

  if (!dst.good()) {
    throw std::runtime_error{"Stream write error"};
  }
}
}  // namespace

export void EncodeIntoFile(Value const& src, std::filesystem::path const& dst) {
  std::ofstream fout{dst, std::ios::binary | std::ios::trunc};
  if (!fout) {
    throw std::runtime_error{"Cannot create file"};
  }
  Encode(src, fout);
}

export void EncodeIntoSpan(Value const& src, std::span<char> dst) {
  std::ospanstream sout{dst};
  Encode(src, sout);
}

export [[nodiscard]] std::string EncodeIntoString(Value const& src) {
  std::ostringstream sout{};
  Encode(src, sout);
  return sout.str();
}
}  // namespace byte_torrent::bencode
