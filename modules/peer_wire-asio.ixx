// Note: This module requires Asio to be installed via vcpkg
// Add "asio" to vcpkg.json dependencies

module;

// Asio must be included in global module fragment
#ifdef BYTE_TORRENT_HAS_ASIO
#define ASIO_STANDALONE
#include <asio.hpp>
#endif

export module peer_wire:asio;

import std;
import :types;
import :connection;

namespace byte_torrent::peer_wire {

#ifdef BYTE_TORRENT_HAS_ASIO

// ============================================================================
// Asio Socket Implementation
// ============================================================================

export class AsioSocket : public ISocket {
 public:
  explicit AsioSocket(asio::io_context& io_ctx)
      : io_ctx_{io_ctx}, socket_{io_ctx} {}

  explicit AsioSocket(asio::io_context& io_ctx, asio::ip::tcp::socket socket)
      : io_ctx_{io_ctx}, socket_{std::move(socket)} {}

  ~AsioSocket() override { Close(); }

  void Connect(PeerEndpoint const& endpoint,
               std::function<void(std::error_code)> callback) override {
    try {
      asio::ip::tcp::resolver resolver{io_ctx_};
      auto endpoints = resolver.resolve(endpoint.ip, std::to_string(endpoint.port));

      asio::async_connect(
          socket_, endpoints,
          [callback = std::move(callback)](std::error_code ec,
                                            asio::ip::tcp::endpoint const&) {
            callback(ec);
          });
    } catch (std::exception const& e) {
      callback(std::make_error_code(std::errc::connection_refused));
    }
  }

  void Close() override {
    std::error_code ec;
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
  }

  [[nodiscard]] bool IsOpen() const override { return socket_.is_open(); }

  void AsyncRead(std::span<std::byte> buffer,
                 std::function<void(std::error_code, std::size_t)> callback) override {
    socket_.async_read_some(
        asio::buffer(buffer.data(), buffer.size()),
        [callback = std::move(callback)](std::error_code ec, std::size_t bytes) {
          callback(ec, bytes);
        });
  }

  void AsyncWrite(std::span<std::byte const> data,
                  std::function<void(std::error_code, std::size_t)> callback) override {
    asio::async_write(
        socket_, asio::buffer(data.data(), data.size()),
        [callback = std::move(callback)](std::error_code ec, std::size_t bytes) {
          callback(ec, bytes);
        });
  }

  void SetReadTimeout(std::chrono::seconds timeout) override {
    read_timeout_ = timeout;
    // Asio doesn't have built-in timeouts; would need timer wrapper
  }

  void SetWriteTimeout(std::chrono::seconds timeout) override {
    write_timeout_ = timeout;
  }

  [[nodiscard]] PeerEndpoint GetRemoteEndpoint() const override {
    try {
      auto const ep = socket_.remote_endpoint();
      return {.ip = ep.address().to_string(),
              .port = ep.port()};
    } catch (...) {
      return {};
    }
  }

  [[nodiscard]] PeerEndpoint GetLocalEndpoint() const override {
    try {
      auto const ep = socket_.local_endpoint();
      return {.ip = ep.address().to_string(),
              .port = ep.port()};
    } catch (...) {
      return {};
    }
  }

  // Access underlying socket for advanced use
  [[nodiscard]] asio::ip::tcp::socket& GetSocket() { return socket_; }

 private:
  asio::io_context& io_ctx_;
  asio::ip::tcp::socket socket_;
  std::chrono::seconds read_timeout_{60};
  std::chrono::seconds write_timeout_{60};
};

// ============================================================================
// Asio Socket Factory
// ============================================================================

export [[nodiscard]] std::unique_ptr<ISocket> CreateAsioSocket(
    asio::io_context& io_ctx) {
  return std::make_unique<AsioSocket>(io_ctx);
}

export [[nodiscard]] std::unique_ptr<ISocket> CreateAsioSocket(
    asio::io_context& io_ctx, asio::ip::tcp::socket socket) {
  return std::make_unique<AsioSocket>(io_ctx, std::move(socket));
}

// ============================================================================
// Connection Listener (for accepting incoming connections)
// ============================================================================

export class ConnectionListener {
 public:
  ConnectionListener(asio::io_context& io_ctx,
                     std::uint16_t port,
                     std::function<void(std::unique_ptr<ISocket>)> on_accept)
      : io_ctx_{io_ctx},
        acceptor_{io_ctx, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), port}},
        on_accept_{std::move(on_accept)} {}

  void Start() {
    DoAccept();
  }

  void Stop() {
    std::error_code ec;
    acceptor_.close(ec);
  }

  [[nodiscard]] std::uint16_t GetPort() const {
    return acceptor_.local_endpoint().port();
  }

 private:
  void DoAccept() {
    acceptor_.async_accept([this](std::error_code ec,
                                   asio::ip::tcp::socket socket) {
      if (!ec) {
        on_accept_(std::make_unique<AsioSocket>(io_ctx_, std::move(socket)));
      }
      if (acceptor_.is_open()) {
        DoAccept();
      }
    });
  }

  asio::io_context& io_ctx_;
  asio::ip::tcp::acceptor acceptor_;
  std::function<void(std::unique_ptr<ISocket>)> on_accept_;
};

#else  // !BYTE_TORRENT_HAS_ASIO

// Stub implementations when Asio is not available
export class AsioSocket : public ISocket {
 public:
  void Connect(PeerEndpoint const&,
               std::function<void(std::error_code)> callback) override {
    callback(std::make_error_code(std::errc::function_not_supported));
  }
  void Close() override {}
  [[nodiscard]] bool IsOpen() const override { return false; }
  void AsyncRead(std::span<std::byte>,
                 std::function<void(std::error_code, std::size_t)> callback) override {
    callback(std::make_error_code(std::errc::function_not_supported), 0);
  }
  void AsyncWrite(std::span<std::byte const>,
                  std::function<void(std::error_code, std::size_t)> callback) override {
    callback(std::make_error_code(std::errc::function_not_supported), 0);
  }
  void SetReadTimeout(std::chrono::seconds) override {}
  void SetWriteTimeout(std::chrono::seconds) override {}
  [[nodiscard]] PeerEndpoint GetRemoteEndpoint() const override { return {}; }
  [[nodiscard]] PeerEndpoint GetLocalEndpoint() const override { return {}; }
};

#endif  // BYTE_TORRENT_HAS_ASIO

}  // namespace byte_torrent::peer_wire
