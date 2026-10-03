#pragma once

#include <cstdint>

#include <windows.h>

namespace wowai::storage {

struct AssistantSettings {
    bool hotkey_enabled{true};
    std::uint32_t hotkey_modifiers{MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT};
    std::uint32_t hotkey_virtual_key{VK_SPACE};
    std::uint32_t overlay_opacity_percent{92};
    std::uint32_t overlay_font_size_px{16};
    bool save_conversation_history{false};

    [[nodiscard]] static AssistantSettings defaults() noexcept;
    [[nodiscard]] bool valid() const noexcept;
    friend bool operator==(const AssistantSettings&, const AssistantSettings&) = default;
};

} // namespace wowai::storage
