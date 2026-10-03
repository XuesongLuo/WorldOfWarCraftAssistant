#pragma once

#include <cstdint>
#include <string>

#include <windows.h>

namespace wowai::storage {

struct AssistantSettings {
    bool hotkey_enabled{true};
    std::uint32_t hotkey_modifiers{MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT};
    std::uint32_t hotkey_virtual_key{VK_SPACE};
    std::uint32_t overlay_opacity_percent{92};
    std::uint32_t overlay_font_size_px{16};
    bool save_conversation_history{false};
    bool cloud_enabled{false};
    std::string cloud_provider{"openai"};
    std::string cloud_model;
    std::string cloud_profile{"default"};
    std::string cloud_organization;
    std::string cloud_region{"singapore"};
    std::string cloud_resource;
    std::string cloud_api_version{"v1"};
    std::uint32_t cloud_session_request_limit{20};
    std::uint32_t cloud_daily_request_limit{100};
    std::uint32_t cloud_monthly_request_limit{1'000};

    [[nodiscard]] static AssistantSettings defaults() noexcept;
    [[nodiscard]] bool valid() const noexcept;
    friend bool operator==(const AssistantSettings&, const AssistantSettings&) = default;
};

} // namespace wowai::storage
