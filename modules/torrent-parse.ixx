export module torrent:parse;

import std;
import bencode;
import :types;
import :hash;

namespace byte_torrent::torrent {
namespace {

[[nodiscard]] bencode::ByteString const& GetByteString(
    bencode::Dictionary const& dict, std::string_view key) {
  auto const it = dict.find(std::string{key});
  if (it == dict.end()) {
    throw std::invalid_argument{
        std::format("Missing required field: '{}'", key)};
  }
  if (!std::holds_alternative<bencode::ByteString>(it->second)) {
    throw std::invalid_argument{
        std::format("Field '{}' must be a byte string", key)};
  }
  return std::get<bencode::ByteString>(it->second);
}

[[nodiscard]] bencode::Integer GetInteger(bencode::Dictionary const& dict,
                                          std::string_view key) {
  auto const it = dict.find(std::string{key});
  if (it == dict.end()) {
    throw std::invalid_argument{
        std::format("Missing required field: '{}'", key)};
  }
  if (!std::holds_alternative<bencode::Integer>(it->second)) {
    throw std::invalid_argument{
        std::format("Field '{}' must be an integer", key)};
  }
  return std::get<bencode::Integer>(it->second);
}

[[nodiscard]] bencode::Dictionary const& GetDictionary(
    bencode::Dictionary const& dict, std::string_view key) {
  auto const it = dict.find(std::string{key});
  if (it == dict.end()) {
    throw std::invalid_argument{
        std::format("Missing required field: '{}'", key)};
  }
  if (!std::holds_alternative<bencode::Dictionary>(it->second)) {
    throw std::invalid_argument{
        std::format("Field '{}' must be a dictionary", key)};
  }
  return std::get<bencode::Dictionary>(it->second);
}

[[nodiscard]] bencode::List const& GetList(bencode::Dictionary const& dict,
                                           std::string_view key) {
  auto const it = dict.find(std::string{key});
  if (it == dict.end()) {
    throw std::invalid_argument{
        std::format("Missing required field: '{}'", key)};
  }
  if (!std::holds_alternative<bencode::List>(it->second)) {
    throw std::invalid_argument{std::format("Field '{}' must be a list", key)};
  }
  return std::get<bencode::List>(it->second);
}

template <typename T, typename Extractor>
[[nodiscard]] std::optional<T> GetOptional(bencode::Dictionary const& dict,
                                           std::string_view key,
                                           Extractor extractor) {
  auto const it = dict.find(std::string{key});
  if (it == dict.end()) {
    return std::nullopt;
  }
  return extractor(it->second);
}

[[nodiscard]] std::vector<Sha1Hash> ParsePieces(
    bencode::ByteString const& raw_pieces) {
  if (!validation::IsValidPiecesData(
          std::span{raw_pieces.data(), raw_pieces.size()})) {
    throw std::invalid_argument{
        "Pieces data length must be a multiple of 20 bytes"};
  }

  std::vector<Sha1Hash> result;
  result.reserve(raw_pieces.size() / kSha1HashSize);

  for (std::size_t i = 0; i < raw_pieces.size(); i += kSha1HashSize) {
    Sha1Hash hash{};
    std::copy_n(raw_pieces.begin() + static_cast<std::ptrdiff_t>(i),
                kSha1HashSize, hash.begin());
    result.push_back(hash);
  }

  return result;
}

[[nodiscard]] FileInfo ParseFileInfo(bencode::Dictionary const& file_dict) {
  auto const length = GetInteger(file_dict, "length");
  if (length < 0) {
    throw std::invalid_argument{"File length cannot be negative"};
  }

  auto const& path_list = GetList(file_dict, "path");
  if (path_list.empty()) {
    throw std::invalid_argument{"File path list cannot be empty"};
  }

  std::filesystem::path path;
  for (auto const& component : path_list) {
    if (!std::holds_alternative<bencode::ByteString>(component)) {
      throw std::invalid_argument{"Path component must be a byte string"};
    }
    path /= bencode::ToString(std::get<bencode::ByteString>(component));
  }

  FileInfo info{.path = std::move(path),
                .length = static_cast<std::uint64_t>(length)};

  info.md5sum = GetOptional<std::string>(
      file_dict, "md5sum", [](bencode::Value const& v) {
        return bencode::ToString(std::get<bencode::ByteString>(v));
      });

  return info;
}

[[nodiscard]] Info ParseInfo(bencode::Dictionary const& info_dict) {
  Info info{};

  info.name = bencode::ToString(GetByteString(info_dict, "name"));

  auto const piece_length = GetInteger(info_dict, "piece length");
  if (piece_length <= 0) {
    throw std::invalid_argument{"Piece length must be positive"};
  }
  info.piece_length = static_cast<std::uint64_t>(piece_length);

  info.pieces = ParsePieces(GetByteString(info_dict, "pieces"));

  // Single-file vs multi-file mode
  if (auto const it = info_dict.find("length"); it != info_dict.end()) {
    // Single-file mode
    if (!std::holds_alternative<bencode::Integer>(it->second)) {
      throw std::invalid_argument{"Field 'length' must be an integer"};
    }
    auto const length = std::get<bencode::Integer>(it->second);
    if (length < 0) {
      throw std::invalid_argument{"Length cannot be negative"};
    }
    info.length = static_cast<std::uint64_t>(length);
  } else if (auto const files_it = info_dict.find("files");
             files_it != info_dict.end()) {
    // Multi-file mode
    if (!std::holds_alternative<bencode::List>(files_it->second)) {
      throw std::invalid_argument{"Field 'files' must be a list"};
    }
    auto const& files_list = std::get<bencode::List>(files_it->second);

    std::vector<FileInfo> files;
    files.reserve(files_list.size());

    for (auto const& file_value : files_list) {
      if (!std::holds_alternative<bencode::Dictionary>(file_value)) {
        throw std::invalid_argument{"Each file entry must be a dictionary"};
      }
      files.push_back(ParseFileInfo(std::get<bencode::Dictionary>(file_value)));
    }
    info.files = std::move(files);
  } else {
    throw std::invalid_argument{
        "Info must contain either 'length' or 'files' field"};
  }

  // Optional: private flag
  info.is_private = GetOptional<bool>(
      info_dict, "private", [](bencode::Value const& v) -> bool {
        return std::get<bencode::Integer>(v) != 0;
      });

  return info;
}

[[nodiscard]] std::vector<std::vector<std::string>> ParseAnnounceList(
    bencode::List const& announce_list) {
  std::vector<std::vector<std::string>> result;
  result.reserve(announce_list.size());

  for (auto const& tier_value : announce_list) {
    if (!std::holds_alternative<bencode::List>(tier_value)) {
      throw std::invalid_argument{"Announce list tier must be a list"};
    }
    auto const& tier_list = std::get<bencode::List>(tier_value);

    std::vector<std::string> tier;
    tier.reserve(tier_list.size());

    for (auto const& tracker_value : tier_list) {
      if (!std::holds_alternative<bencode::ByteString>(tracker_value)) {
        throw std::invalid_argument{"Tracker URL must be a byte string"};
      }
      tier.push_back(
          bencode::ToString(std::get<bencode::ByteString>(tracker_value)));
    }
    result.push_back(std::move(tier));
  }

  return result;
}

[[nodiscard]] Torrent ParseTorrent(bencode::Value const& root) {
  if (!std::holds_alternative<bencode::Dictionary>(root)) {
    throw std::invalid_argument{"Torrent file must be a dictionary"};
  }
  auto const& root_dict = std::get<bencode::Dictionary>(root);

  Torrent torrent{};

  // Required: announce
  torrent.announce = bencode::ToString(GetByteString(root_dict, "announce"));

  // Required: info
  auto const& info_dict = GetDictionary(root_dict, "info");
  torrent.info = ParseInfo(info_dict);
  torrent.info_hash = CalculateInfoHash(info_dict);

  // Optional: announce-list (BEP-0012)
  torrent.announce_list = GetOptional<std::vector<std::vector<std::string>>>(
      root_dict, "announce-list", [](bencode::Value const& v) {
        return ParseAnnounceList(std::get<bencode::List>(v));
      });

  // Optional: creation date
  torrent.creation_date = GetOptional<std::chrono::system_clock::time_point>(
      root_dict, "creation date", [](bencode::Value const& v) {
        auto const timestamp = std::get<bencode::Integer>(v);
        return std::chrono::system_clock::from_time_t(
            static_cast<std::time_t>(timestamp));
      });

  // Optional: comment
  torrent.comment = GetOptional<std::string>(
      root_dict, "comment", [](bencode::Value const& v) {
        return bencode::ToString(std::get<bencode::ByteString>(v));
      });

  // Optional: created by
  torrent.created_by = GetOptional<std::string>(
      root_dict, "created by", [](bencode::Value const& v) {
        return bencode::ToString(std::get<bencode::ByteString>(v));
      });

  // Optional: encoding
  torrent.encoding = GetOptional<std::string>(
      root_dict, "encoding", [](bencode::Value const& v) {
        return bencode::ToString(std::get<bencode::ByteString>(v));
      });

  return torrent;
}

}  // namespace

export [[nodiscard]] Torrent ParseFromFile(std::filesystem::path const& src) {
  return ParseTorrent(bencode::DecodeFromFile(src));
}

export [[nodiscard]] Torrent ParseFromString(std::string_view src) {
  return ParseTorrent(bencode::DecodeFromString(src));
}

export [[nodiscard]] Torrent ParseFromSpan(std::span<char const> src) {
  return ParseTorrent(bencode::DecodeFromSpan(src));
}
}  // namespace byte_torrent::torrent
