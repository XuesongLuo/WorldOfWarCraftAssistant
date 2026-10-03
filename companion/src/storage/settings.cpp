#include "wowai/storage/settings.hpp"

namespace wowai::storage {

AssistantSettings AssistantSettings::defaults() noexcept { return {}; }

bool AssistantSettings::valid() const noexcept {
    constexpr std::uint32_t allowed_modifiers =
        MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN | MOD_NOREPEAT;
    const bool supported_key = hotkey_virtual_key == VK_SPACE ||
                               (hotkey_virtual_key >= static_cast<std::uint32_t>('A') &&
                                hotkey_virtual_key <= static_cast<std::uint32_t>('Z')) ||
                               (hotkey_virtual_key >= VK_F1 && hotkey_virtual_key <= VK_F24);
    const auto meaningful_modifiers = hotkey_modifiers & ~static_cast<std::uint32_t>(MOD_NOREPEAT);
    return (hotkey_modifiers & ~allowed_modifiers) == 0 && meaningful_modifiers != 0 &&
           supported_key && overlay_opacity_percent >= 20 && overlay_opacity_percent <= 100 &&
           overlay_font_size_px >= 12 && overlay_font_size_px <= 28;
}

} // namespace wowai::storage
