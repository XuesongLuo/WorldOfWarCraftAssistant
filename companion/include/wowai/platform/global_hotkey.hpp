#pragma once

#include "wowai/storage/settings.hpp"

#include <cstdint>

#include <windows.h>

namespace wowai::platform {

inline constexpr int assistant_hotkey_id = 0x5741;

class HotkeyRegistrar {
  public:
    virtual ~HotkeyRegistrar() = default;
    [[nodiscard]] virtual bool register_hotkey(HWND window, int identifier, std::uint32_t modifiers,
                                               std::uint32_t virtual_key) noexcept = 0;
    virtual void unregister_hotkey(HWND window, int identifier) noexcept = 0;
};

class WindowsHotkeyRegistrar final : public HotkeyRegistrar {
  public:
    [[nodiscard]] bool register_hotkey(HWND window, int identifier, std::uint32_t modifiers,
                                       std::uint32_t virtual_key) noexcept override;
    void unregister_hotkey(HWND window, int identifier) noexcept override;
};

enum class HotkeyApplyResult { registered, disabled, conflict, invalid };

class GlobalHotkey final {
  public:
    GlobalHotkey(HWND window, HotkeyRegistrar& registrar) noexcept;
    ~GlobalHotkey();

    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    [[nodiscard]] HotkeyApplyResult
    apply(const wowai::storage::AssistantSettings& settings) noexcept;
    void clear() noexcept;
    [[nodiscard]] bool registered() const noexcept;

  private:
    HWND window_{};
    HotkeyRegistrar* registrar_{};
    bool registered_{};
};

} // namespace wowai::platform
