export module bencode:decode;

import std;
import :types;  // Import types partition

namespace byte_torrent::bencode {
namespace {
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
    throw std::runtime_error{
        "Unexpected end of stream while reading byte string"};
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
    throw std::runtime_error{
        "Unexpected end of stream while reading dictionary"};
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

  switch (int const next_char = src.peek(); next_char) {
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
}  // namespace byte_torrent::bencode
