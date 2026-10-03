#pragma once

#include "wowai/codex/host_process.hpp"
#include "wowai/codex/protocol.hpp"

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <stop_token>

namespace wowai::codex {

enum class HostClientFailureKind { timeout, exited, protocol, cancelled };

class HostClientFailure final : public std::runtime_error {
  public:
    HostClientFailure(HostClientFailureKind kind, std::string message)
        : std::runtime_error(std::move(message)), kind_(kind) {}

    [[nodiscard]] HostClientFailureKind kind() const noexcept { return kind_; }

  private:
    HostClientFailureKind kind_;
};

class HostClient final {
  public:
    explicit HostClient(const HostLaunchOptions& options) : process_(options) {}

    [[nodiscard]] nlohmann::json negotiate(const nlohmann::json& hello,
                                           std::chrono::milliseconds timeout);
    [[nodiscard]] nlohmann::json request(const nlohmann::json& request,
                                         std::chrono::milliseconds timeout,
                                         std::stop_token stop_token = {});
    void cancel(const nlohmann::json& cancel);
    [[nodiscard]] HostProcess& process() noexcept { return process_; }

  private:
    [[nodiscard]] nlohmann::json exchange(const nlohmann::json& outbound,
                                          std::string_view expected_kind,
                                          std::chrono::milliseconds timeout,
                                          std::stop_token stop_token = {});

    HostProcess process_;
    ProtocolSequenceTracker sequence_;
    std::mutex sequence_mutex_;
};

} // namespace wowai::codex
