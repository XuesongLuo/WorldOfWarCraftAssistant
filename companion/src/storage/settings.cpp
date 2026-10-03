#include "wowai/storage/settings.hpp"

#include <algorithm>

namespace wowai::storage {

namespace {

bool valid_ascii(const std::string& value, const std::size_t maximum, const bool allow_empty) {
    if (value.size() > maximum || (!allow_empty && value.empty()))
        return false;
    return std::all_of(value.begin(), value.end(), [](const unsigned char character) {
        return character >= 0x20 && character <= 0x7e;
    });
}

bool valid_identifier(const std::string& value, const std::size_t maximum, const bool allow_empty) {
    if (value.size() > maximum || (!allow_empty && value.empty()))
        return false;
    return std::all_of(value.begin(), value.end(), [](const unsigned char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '-' || character == '_' ||
               character == '.';
    });
}

bool valid_dns_identifier(const std::string& value, const std::size_t maximum,
                          const bool allow_empty) {
    if (value.empty())
        return allow_empty;
    if (value.size() > maximum || value.front() == '-' || value.back() == '-')
        return false;
    return std::all_of(value.begin(), value.end(), [](const unsigned char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '-';
    });
}

bool supported_cloud_provider(const std::string& value) {
    return value == "openai" || value == "deepseek" || value == "xai" || value == "openrouter" ||
           value == "dashscope" || value == "azure-openai";
}

bool supported_dashscope_region(const std::string& value) {
    return value == "beijing" || value == "hongkong" || value == "singapore" || value == "tokyo" ||
           value == "frankfurt" || value == "virginia";
}

} // namespace

AssistantSettings AssistantSettings::defaults() noexcept { return {}; }

bool AssistantSettings::valid() const noexcept {
    constexpr std::uint32_t allowed_modifiers =
        MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN | MOD_NOREPEAT;
    const bool supported_key = hotkey_virtual_key == VK_SPACE ||
                               (hotkey_virtual_key >= static_cast<std::uint32_t>('A') &&
                                hotkey_virtual_key <= static_cast<std::uint32_t>('Z')) ||
                               (hotkey_virtual_key >= VK_F1 && hotkey_virtual_key <= VK_F24);
    const auto meaningful_modifiers = hotkey_modifiers & ~static_cast<std::uint32_t>(MOD_NOREPEAT);
    const bool cloud_metadata =
        supported_cloud_provider(cloud_provider) &&
        valid_dns_identifier(cloud_profile, 32, false) &&
        valid_ascii(cloud_model, 128, !cloud_enabled) &&
        valid_identifier(cloud_organization, 128, true) &&
        valid_identifier(cloud_region, 32, false) &&
        valid_dns_identifier(cloud_resource, 64, true) &&
        valid_identifier(cloud_api_version, 32, false) &&
        (cloud_provider != "dashscope" || !cloud_enabled ||
         (!cloud_resource.empty() && supported_dashscope_region(cloud_region))) &&
        (cloud_provider != "azure-openai" || !cloud_enabled || !cloud_resource.empty());
    const bool limits = cloud_session_request_limit >= 1 && cloud_session_request_limit <= 10'000 &&
                        cloud_daily_request_limit >= cloud_session_request_limit &&
                        cloud_daily_request_limit <= 100'000 &&
                        cloud_monthly_request_limit >= cloud_daily_request_limit &&
                        cloud_monthly_request_limit <= 1'000'000 &&
                        cloud_stop_threshold_percent >= 1 && cloud_stop_threshold_percent <= 100;
    return (hotkey_modifiers & ~allowed_modifiers) == 0 && meaningful_modifiers != 0 &&
           supported_key && overlay_opacity_percent >= 20 && overlay_opacity_percent <= 100 &&
           overlay_font_size_px >= 12 && overlay_font_size_px <= 28 && cloud_metadata && limits;
}

} // namespace wowai::storage
