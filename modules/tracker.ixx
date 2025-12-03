export module tracker;

export import :types;
export import :url;
export import :response;
export import :http;

import std;
import peer_wire;

namespace byte_torrent::tracker {

// ============================================================================
// Tracker Client
// ============================================================================

export class TrackerClient {
 public:
  explicit TrackerClient(std::string announce_url,
                         std::unique_ptr<IHttpClient> http_client = nullptr)
      : announce_url_{std::move(announce_url)},
        http_client_{std::move(http_client)} {}

  // Perform announce request
  [[nodiscard]] TrackerResult<AnnounceResponse> Announce(
      AnnounceRequest const& request,
      std::chrono::seconds timeout = std::chrono::seconds{30}) {
    if (!http_client_) {
      return MakeError<AnnounceResponse>(TrackerErrorCode::NetworkError,
                                          "No HTTP client configured");
    }

    if (!request.IsValid()) {
      return MakeError<AnnounceResponse>(TrackerErrorCode::InvalidResponse,
                                          "Invalid announce request");
    }

    Url url{};
    try {
      url = BuildAnnounceUrl(announce_url_, request);
    } catch (std::exception const& e) {
      return MakeError<AnnounceResponse>(TrackerErrorCode::InvalidResponse,
                                          e.what());
    }

    auto http_result = http_client_->Get(url, timeout);
    if (!http_result) {
      return MakeError<AnnounceResponse>(http_result.error.code,
                                          http_result.error.message);
    }

    if (!http_result->IsSuccess()) {
      return MakeError<AnnounceResponse>(
          TrackerErrorCode::HttpError,
          std::format("HTTP error: {}", http_result->status_code));
    }

    return ParseAnnounceResponse(
        std::span{http_result->body.data(), http_result->body.size()});
  }

  // Get scrape URL (if supported)
  [[nodiscard]] std::optional<std::string> GetScrapeUrl() const {
    return tracker::GetScrapeUrl(announce_url_);
  }

  // Perform scrape request
  [[nodiscard]] TrackerResult<ScrapeResponse> Scrape(
      ScrapeRequest const& request,
      std::chrono::seconds timeout = std::chrono::seconds{30}) {
    if (!http_client_) {
      return MakeError<ScrapeResponse>(TrackerErrorCode::NetworkError,
                                        "No HTTP client configured");
    }

    auto const scrape_url = GetScrapeUrl();
    if (!scrape_url) {
      return MakeError<ScrapeResponse>(TrackerErrorCode::InvalidResponse,
                                        "Tracker does not support scrape");
    }

    auto url = ParseUrl(*scrape_url);
    if (!url) {
      return MakeError<ScrapeResponse>(TrackerErrorCode::InvalidResponse,
                                        "Invalid scrape URL");
    }

    // Build query with info_hashes
    QueryBuilder query{};
    for (auto const& hash : request.info_hashes) {
      query.Add("info_hash", std::span{hash});
    }
    url->query = query.Build();

    auto http_result = http_client_->Get(*url, timeout);
    if (!http_result) {
      return MakeError<ScrapeResponse>(http_result.error.code,
                                        http_result.error.message);
    }

    if (!http_result->IsSuccess()) {
      return MakeError<ScrapeResponse>(
          TrackerErrorCode::HttpError,
          std::format("HTTP error: {}", http_result->status_code));
    }

    return ParseScrapeResponse(
        std::span{http_result->body.data(), http_result->body.size()});
  }

  // Set HTTP client (for dependency injection)
  void SetHttpClient(std::unique_ptr<IHttpClient> client) {
    http_client_ = std::move(client);
  }

  [[nodiscard]] std::string const& GetAnnounceUrl() const noexcept {
    return announce_url_;
  }

 private:
  std::string announce_url_{};
  std::unique_ptr<IHttpClient> http_client_{};
};

// ============================================================================
// Multi-Tracker Support (BEP-12)
// ============================================================================

export class MultiTrackerClient {
 public:
  explicit MultiTrackerClient(
      std::vector<std::vector<std::string>> announce_list,
      std::unique_ptr<IHttpClient> http_client = nullptr)
      : http_client_{std::move(http_client)} {
    for (auto& tier : announce_list) {
      if (!tier.empty()) {
        tiers_.push_back(std::move(tier));
      }
    }
  }

  // Try announce across all tiers (with shuffling within tiers)
  [[nodiscard]] TrackerResult<AnnounceResponse> Announce(
      AnnounceRequest const& request,
      std::chrono::seconds timeout = std::chrono::seconds{30}) {
    if (tiers_.empty()) {
      return MakeError<AnnounceResponse>(TrackerErrorCode::InvalidResponse,
                                          "No trackers configured");
    }

    TrackerError last_error{};

    for (auto& tier : tiers_) {
      // Shuffle tier for load balancing
      std::random_device rd{};
      std::mt19937 gen{rd()};
      std::ranges::shuffle(tier, gen);

      for (auto const& url : tier) {
        TrackerClient client{url, CloneHttpClient()};
        auto result = client.Announce(request, timeout);

        if (result) {
          // Move successful tracker to front of tier
          if (&url != &tier.front()) {
            auto const it = std::ranges::find(tier, url);
            if (it != tier.end()) {
              std::rotate(tier.begin(), it, it + 1);
            }
          }
          return result;
        }

        last_error = result.error;
      }
    }

    return MakeError<AnnounceResponse>(last_error.code, last_error.message);
  }

  void SetHttpClient(std::unique_ptr<IHttpClient> client) {
    http_client_ = std::move(client);
  }

  [[nodiscard]] std::size_t TierCount() const noexcept { return tiers_.size(); }

  [[nodiscard]] std::size_t TotalTrackerCount() const noexcept {
    std::size_t count{0};
    for (auto const& tier : tiers_) {
      count += tier.size();
    }
    return count;
  }

 private:
  [[nodiscard]] std::unique_ptr<IHttpClient> CloneHttpClient() const {
    // For now, return nullptr - real implementation would clone or share
    return nullptr;
  }

  std::vector<std::vector<std::string>> tiers_{};
  std::unique_ptr<IHttpClient> http_client_{};
};

}  // namespace byte_torrent::tracker
