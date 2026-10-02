#pragma once

#include "wowai/codex/host_client.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace wowai::codex {

class AssistantSession final {
  public:
    AssistantSession(const HostLaunchOptions& options, std::string provider, std::string model);

    AssistantSession(const AssistantSession&) = delete;
    AssistantSession& operator=(const AssistantSession&) = delete;

    [[nodiscard]] std::string ask(std::string_view question,
                                  std::chrono::milliseconds timeout);

    [[nodiscard]] static std::unique_ptr<AssistantSession> from_environment();

  private:
    HostClient client_;
    std::string conversation_id_;
    std::string provider_;
    std::string model_;
};

[[nodiscard]] nlohmann::json make_assistant_request(std::string_view request_id,
                                                     std::string_view conversation_id,
                                                     std::string_view created_at,
                                                     std::string_view question,
                                                     std::string_view provider,
                                                     std::string_view model);

} // namespace wowai::codex
