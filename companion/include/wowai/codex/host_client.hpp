#pragma once

#include "wowai/codex/host_process.hpp"
#include "wowai/codex/protocol.hpp"

#include <chrono>

namespace wowai::codex {

class HostClient final {
  public:
    explicit HostClient(const HostLaunchOptions& options) : process_(options) {}

    [[nodiscard]] nlohmann::json negotiate(const nlohmann::json& hello,
                                           std::chrono::milliseconds timeout);
    [[nodiscard]] nlohmann::json request(const nlohmann::json& request,
                                         std::chrono::milliseconds timeout);
    void cancel(const nlohmann::json& cancel);
    [[nodiscard]] HostProcess& process() noexcept { return process_; }

  private:
    [[nodiscard]] nlohmann::json exchange(const nlohmann::json& outbound,
                                          std::string_view expected_kind,
                                          std::chrono::milliseconds timeout);

    HostProcess process_;
    ProtocolSequenceTracker sequence_;
};

} // namespace wowai::codex
