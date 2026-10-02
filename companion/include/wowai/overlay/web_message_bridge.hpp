#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace wowai::overlay {

inline constexpr std::size_t maximum_web_message_bytes = 16 * 1024;

enum class WebMessageKind { ready, send_message, set_interaction, open_external };

struct WebMessage {
    WebMessageKind kind{};
    std::string text;
    bool enabled{};
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

} // namespace wowai::overlay
