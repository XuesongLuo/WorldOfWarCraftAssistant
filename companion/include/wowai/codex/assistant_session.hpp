#pragma once

#include "wowai/codex/host_client.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>

namespace wowai::codex {

class AssistantFailure final : public std::runtime_error {
  public:
    AssistantFailure(std::string code, std::string message, bool retryable,
                     bool host_recovery_recommended = false)
        : std::runtime_error(std::move(message)), code_(std::move(code)), retryable_(retryable),
          host_recovery_recommended_(host_recovery_recommended) {}

    [[nodiscard]] std::string_view code() const noexcept { return code_; }
    [[nodiscard]] bool retryable() const noexcept { return retryable_; }
    [[nodiscard]] bool host_recovery_recommended() const noexcept {
        return host_recovery_recommended_;
    }

  private:
    std::string code_;
    bool retryable_{};
    bool host_recovery_recommended_{};
};

class AssistantRequestCancelled final : public std::runtime_error {
  public:
    AssistantRequestCancelled() : std::runtime_error("assistant request cancelled") {}
};

struct ConfirmedImage {
    std::string id;
    std::string mime_type;
    std::string capture_scope;
    std::string sha256;
    std::string data_base64;
    bool privacy_mask_applied{};
    std::string upload_confirmed_at;
};

class AssistantSession final {
  public:
    AssistantSession(const HostLaunchOptions& options, std::string provider, std::string model);

    AssistantSession(const AssistantSession&) = delete;
    AssistantSession& operator=(const AssistantSession&) = delete;

    [[nodiscard]] std::string ask(std::string_view question,
                                  std::chrono::milliseconds timeout,
                                  std::optional<ConfirmedImage> image = std::nullopt,
                                  nlohmann::json observations = nlohmann::json::array(),
                                  nlohmann::json visual_bridge = nullptr,
                                  bool screen_observation_enabled = false,
                                  std::stop_token stop_token = {});

    void cancel_active_request() noexcept;
    void recover();

    [[nodiscard]] static std::unique_ptr<AssistantSession> from_environment();
    [[nodiscard]] std::string_view provider() const noexcept { return provider_; }

  private:
    void connect();

    HostLaunchOptions options_;
    std::unique_ptr<HostClient> client_;
    std::string conversation_id_;
    std::string provider_;
    std::string model_;
    std::mutex active_mutex_;
    std::optional<std::string> active_request_id_;
};

[[nodiscard]] nlohmann::json make_assistant_request(std::string_view request_id,
                                                     std::string_view conversation_id,
                                                     std::string_view created_at,
                                                     std::string_view question,
                                                     std::string_view provider,
                                                     std::string_view model,
                                                     std::optional<ConfirmedImage> image = std::nullopt,
                                                     nlohmann::json observations = nlohmann::json::array(),
                                                     nlohmann::json visual_bridge = nullptr,
                                                     bool screen_observation_enabled = false);

} // namespace wowai::codex
