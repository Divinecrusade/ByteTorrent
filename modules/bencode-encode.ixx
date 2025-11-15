export module bencode:encode;

import std;
import :types;  // Import types partition

namespace byte_torrent::bencode {
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
