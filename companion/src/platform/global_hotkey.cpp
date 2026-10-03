#include "wowai/platform/global_hotkey.hpp"

namespace wowai::platform {

bool WindowsHotkeyRegistrar::register_hotkey(const HWND window, const int identifier,
                                             const std::uint32_t modifiers,
                                             const std::uint32_t virtual_key) noexcept {
    return ::RegisterHotKey(window, identifier, modifiers, virtual_key) != FALSE;
}

void WindowsHotkeyRegistrar::unregister_hotkey(const HWND window, const int identifier) noexcept {
    ::UnregisterHotKey(window, identifier);
}

GlobalHotkey::GlobalHotkey(const HWND window, HotkeyRegistrar& registrar) noexcept
    : window_(window), registrar_(&registrar) {}

GlobalHotkey::~GlobalHotkey() { clear(); }

HotkeyApplyResult GlobalHotkey::apply(const wowai::storage::AssistantSettings& settings) noexcept {
    if (!settings.valid()) {
        return HotkeyApplyResult::invalid;
    }
    clear();
    if (!settings.hotkey_enabled) {
        return HotkeyApplyResult::disabled;
    }
    registered_ = registrar_->register_hotkey(
        window_, assistant_hotkey_id, settings.hotkey_modifiers, settings.hotkey_virtual_key);
    return registered_ ? HotkeyApplyResult::registered : HotkeyApplyResult::conflict;
}

void GlobalHotkey::clear() noexcept {
    if (registered_) {
        registrar_->unregister_hotkey(window_, assistant_hotkey_id);
        registered_ = false;
    }
}

bool GlobalHotkey::registered() const noexcept { return registered_; }

} // namespace wowai::platform
