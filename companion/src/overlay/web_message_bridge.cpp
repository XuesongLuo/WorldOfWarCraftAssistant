#include "wowai/overlay/web_message_bridge.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>

#include <nlohmann/json.hpp>

namespace wowai::overlay {
namespace {

using Json = nlohmann::json;

[[nodiscard]] bool exact_keys(const Json& value,
                              const std::set<std::string, std::less<>>& expected) {
    if (!value.is_object() || value.size() != expected.size()) {
        return false;
    }
    for (const auto& [key, ignored] : value.items()) {
        static_cast<void>(ignored);
        if (!expected.contains(key)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string lowercase(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

[[nodiscard]] bool is_one_of(const Json& value, std::initializer_list<std::string_view> choices) {
    if (!value.is_string()) {
        return false;
    }
    const auto& text = value.get_ref<const std::string&>();
    return std::ranges::any_of(choices,
                               [&](const std::string_view choice) { return text == choice; });
}

} // namespace

BridgeResult parse_web_message(const std::string_view source, const std::string_view json_text,
                               const std::string_view expected_source) noexcept {
    try {
        if (expected_source.empty() || source != expected_source) {
            return {std::nullopt, "unexpected message source"};
        }
        if (json_text.empty() || json_text.size() > maximum_web_message_bytes) {
            return {std::nullopt, "message size is outside the allowed range"};
        }

        const Json root = Json::parse(json_text.begin(), json_text.end());
        if (!exact_keys(root, {"payload", "type", "version"}) || root.at("version") != 1 ||
            !root.at("type").is_string()) {
            return {std::nullopt, "invalid message envelope"};
        }
        const Json& payload = root.at("payload");
        const std::string type = root.at("type").get<std::string>();

        if (type == "ready" && exact_keys(payload, {})) {
            return {WebMessage{WebMessageKind::ready}, {}};
        }
        if (type == "send_message" && exact_keys(payload, {"text"}) &&
            payload.at("text").is_string()) {
            std::string text = payload.at("text").get<std::string>();
            if (text.empty() || text.size() > 4000) {
                return {std::nullopt, "text is outside the allowed range"};
            }
            return {WebMessage{WebMessageKind::send_message, std::move(text)}, {}};
        }
        if (type == "cancel_request" && exact_keys(payload, {})) {
            return {WebMessage{WebMessageKind::cancel_request}, {}};
        }
        if (type == "set_interaction" && exact_keys(payload, {"enabled"}) &&
            payload.at("enabled").is_boolean()) {
            return {
                WebMessage{WebMessageKind::set_interaction, {}, payload.at("enabled").get<bool>()},
                {}};
        }
        if (type == "open_external" && exact_keys(payload, {"url"}) &&
            payload.at("url").is_string()) {
            std::string url = payload.at("url").get<std::string>();
            if (!is_allowed_external_url(url)) {
                return {std::nullopt, "external URL is not allowlisted"};
            }
            return {WebMessage{WebMessageKind::open_external, std::move(url)}, {}};
        }
        if (type == "capture_screenshot" && exact_keys(payload, {"maskChat", "selectedRegion"}) &&
            payload.at("maskChat").is_boolean() && payload.at("selectedRegion").is_boolean()) {
            WebMessage message{WebMessageKind::capture_screenshot};
            message.enabled = payload.at("maskChat").get<bool>();
            message.selected_region = payload.at("selectedRegion").get<bool>();
            return {std::move(message), {}};
        }
        if (type == "confirm_screenshot" && exact_keys(payload, {})) {
            return {WebMessage{WebMessageKind::confirm_screenshot}, {}};
        }
        if (type == "discard_screenshot" && exact_keys(payload, {})) {
            return {WebMessage{WebMessageKind::discard_screenshot}, {}};
        }
        if (type == "set_observation" && exact_keys(payload, {"mode"}) &&
            is_one_of(payload.at("mode"), {"scene", "coaching"})) {
            return {
                WebMessage{WebMessageKind::set_observation, payload.at("mode").get<std::string>()},
                {}};
        }
        if (type == "pause_observation" && exact_keys(payload, {})) {
            return {WebMessage{WebMessageKind::pause_observation}, {}};
        }
        return {std::nullopt, "unknown or malformed message"};
    } catch (...) {
        return {std::nullopt, "message is not valid JSON"};
    }
}

bool is_allowed_external_url(const std::string_view url) noexcept {
    if (url.size() > 2048 || !url.starts_with("https://")) {
        return false;
    }
    const std::size_t host_begin = 8;
    const std::size_t host_end = url.find_first_of("/:?#", host_begin);
    const std::string host = lowercase(std::string{url.substr(host_begin, host_end - host_begin)});
    constexpr std::array allowed_hosts{"worldofwarcraft.blizzard.com", "news.blizzard.com",
                                       "support.blizzard.com"};
    for (const std::string_view allowed : allowed_hosts) {
        if (host == allowed) {
            return true;
        }
    }
    return false;
}

std::string make_status_message(const std::string_view text, const bool error) {
    return Json{{"type", "status"}, {"error", error}, {"text", text}, {"version", 1}}.dump();
}

std::string make_assistant_message(const std::string_view text) {
    return Json{{"type", "assistant_message"}, {"text", text}, {"version", 1}}.dump();
}

std::string make_request_state_message(const std::string_view phase, const std::string_view text,
                                       const std::string_view error_code, const bool retryable,
                                       const std::string_view action) {
    return Json{
        {"type", "request_state"}, {"version", 1},           {"phase", phase},  {"text", text},
        {"errorCode", error_code}, {"retryable", retryable}, {"action", action}}
        .dump();
}

std::string make_screenshot_preview(const std::string_view png_base64, const std::int32_t width,
                                    const std::int32_t height, const bool privacy_mask_applied) {
    return Json{{"type", "screenshot_preview"},
                {"version", 1},
                {"dataUrl", "data:image/png;base64," + std::string{png_base64}},
                {"width", width},
                {"height", height},
                {"privacyMaskApplied", privacy_mask_applied},
                {"confirmed", false}}
        .dump();
}

std::string make_screenshot_cleared() {
    return Json{{"type", "screenshot_cleared"}, {"version", 1}}.dump();
}

std::string make_appearance_message(const std::uint32_t opacity_percent,
                                    const std::uint32_t font_size_px) {
    return Json{{"type", "appearance"},
                {"version", 1},
                {"opacity", opacity_percent},
                {"fontSize", font_size_px}}
        .dump();
}

std::string make_observation_message(const std::string_view mode, const std::string_view scene,
                                     const std::string_view captured_at, const double confidence,
                                     const std::string_view reason) {
    return Json{{"type", "observation"},
                {"version", 1},
                {"mode", mode},
                {"scene", scene},
                {"capturedAt", captured_at},
                {"confidence", confidence},
                {"source", "screen-observed"},
                {"reason", reason}}
        .dump();
}

} // namespace wowai::overlay
