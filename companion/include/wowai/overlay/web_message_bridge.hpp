#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace wowai::overlay {

inline constexpr std::size_t maximum_web_message_bytes = 16 * 1024;

enum class WebMessageKind {
    ready,
    send_message,
    cancel_request,
    set_interaction,
    open_external,
    capture_screenshot,
    confirm_screenshot,
    discard_screenshot,
    set_observation,
    pause_observation,
};

struct WebMessage {
    WebMessageKind kind{};
    std::string text;
    bool enabled{};
    bool selected_region{};
};

struct BridgeResult {
    std::optional<WebMessage> message;
    std::string error;
};

[[nodiscard]] BridgeResult
parse_web_message(std::string_view source, std::string_view json_text,
                  std::string_view expected_source = "about:blank") noexcept;
[[nodiscard]] bool is_allowed_external_url(std::string_view url) noexcept;
[[nodiscard]] std::string make_status_message(std::string_view text, bool error = false);
[[nodiscard]] std::string make_assistant_message(std::string_view text);
[[nodiscard]] std::string make_request_state_message(std::string_view phase, std::string_view text,
                                                     std::string_view error_code = {},
                                                     bool retryable = false,
                                                     std::string_view action = {});
[[nodiscard]] std::string make_screenshot_preview(std::string_view png_base64, std::int32_t width,
                                                  std::int32_t height, bool privacy_mask_applied);
[[nodiscard]] std::string make_screenshot_cleared();
[[nodiscard]] std::string make_appearance_message(std::uint32_t opacity_percent,
                                                  std::uint32_t font_size_px);
[[nodiscard]] std::string make_observation_message(std::string_view mode, std::string_view scene,
                                                   std::string_view captured_at, double confidence,
                                                   std::string_view reason);

} // namespace wowai::overlay
