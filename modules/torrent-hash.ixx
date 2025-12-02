export module torrent:hash;

import std;
import <cassert>;
import bencode;
import :types;

// OpenSSL headers (must be outside module purview)
extern "C" {
struct evp_md_ctx_st;
struct evp_md_st;

evp_md_ctx_st* EVP_MD_CTX_new();
void EVP_MD_CTX_free(evp_md_ctx_st* ctx);
evp_md_st const* EVP_sha1();
int EVP_DigestInit_ex(evp_md_ctx_st* ctx, evp_md_st const* type, void* impl);
int EVP_DigestUpdate(evp_md_ctx_st* ctx, void const* data, std::size_t count);
int EVP_DigestFinal_ex(evp_md_ctx_st* ctx, unsigned char* md, unsigned int* s);
}

namespace byte_torrent::torrent {
namespace {
class Sha1Hasher {
 public:
  Sha1Hasher() : ctx_{EVP_MD_CTX_new()} {
    assert(ctx_);
    if (EVP_DigestInit_ex(ctx_, EVP_sha1(), nullptr) != 1) {
      EVP_MD_CTX_free(ctx_);
      throw std::runtime_error{"Failed to initialize SHA1 context"};
    }
  }

  ~Sha1Hasher() {
    assert(ctx_);
    EVP_MD_CTX_free(ctx_);
  }

  Sha1Hasher(Sha1Hasher const&) = delete;
  Sha1Hasher& operator=(Sha1Hasher const&) = delete;

  void Update(std::span<char const> data) {
    if (EVP_DigestUpdate(ctx_, data.data(), data.size()) != 1) {
      throw std::runtime_error{"SHA1 update failed"};
    }
  }

  void Update(std::span<std::byte const> data) {
    if (EVP_DigestUpdate(ctx_, data.data(), data.size()) != 1) {
      throw std::runtime_error{"SHA1 update failed"};
    }
  }

  [[nodiscard]] Sha1Hash Finalize() {
    Sha1Hash result{};
    if (unsigned int len{};
        EVP_DigestFinal_ex(ctx_, 
                           reinterpret_cast<unsigned char*>(result.data()), 
                           &len) != 1) {
      throw std::runtime_error{"SHA1 finalize failed"};
    }
    return result;
  }

 private:
  evp_md_ctx_st* ctx_{};
};
}  // namespace

export [[nodiscard]] Sha1Hash CalculateInfoHash(
    bencode::Dictionary const& info_dict) {
  auto const encoded = bencode::EncodeIntoString(bencode::Value{info_dict});

  Sha1Hasher hasher{};
  hasher.Update(std::span<char const>{encoded.data(), encoded.size()});
  return hasher.Finalize();
}

export [[nodiscard]] Sha1Hash CalculateSha1(std::span<std::byte const> data) {
  Sha1Hasher hasher{};
  hasher.Update(data);
  return hasher.Finalize();
}

export [[nodiscard]] Sha1Hash CalculateSha1(std::span<char const> data) {
  Sha1Hasher hasher{};
  hasher.Update(data);
  return hasher.Finalize();
}

export [[nodiscard]] std::string Sha1ToHex(Sha1Hash const& hash) {
  std::string result;
  result.reserve(kSha1HashSize * 2);

  for (std::byte const b : hash) {
    std::format_to(std::back_inserter(result), "{:02x}",
                   std::to_integer<unsigned>(b));
  }

  return result;
}

export [[nodiscard]] Sha1Hash Sha1FromHex(std::string_view hex) {
  if (hex.size() != kSha1HashSize * 2) {
    throw std::invalid_argument{"Invalid hex string length for SHA1"};
  }

  Sha1Hash result{};
  for (std::size_t i = 0; i != kSha1HashSize; ++i) {
    auto const byte_str = hex.substr(i * 2, 2);
    unsigned int byte_val{};
    auto [ptr, ec] =
        std::from_chars(byte_str.data(), byte_str.data() + 2, byte_val, 16);
    if (ec != std::errc{}) {
      throw std::invalid_argument{"Invalid hex character in SHA1 string"};
    }
    result[i] = static_cast<std::byte>(byte_val);
  }

  return result;
}
}  // namespace byte_torrent::torrent
