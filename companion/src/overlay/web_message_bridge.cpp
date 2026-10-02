#include "wowai/overlay/web_message_bridge.hpp"

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

} // namespace wowai::overlay
