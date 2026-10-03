#pragma once

#include "wowai/storage/settings.hpp"

#include <functional>
#include <memory>

#include <windows.h>

namespace wowai::app {

class SettingsWindow final {
  public:
    using SaveHandler = std::function<bool(const wowai::storage::AssistantSettings&)>;
    using DeleteHandler = std::function<bool()>;

    SettingsWindow(HINSTANCE instance, HWND owner, SaveHandler save_handler,
                   DeleteHandler delete_handler);
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    void show(const wowai::storage::AssistantSettings& settings) noexcept;
    [[nodiscard]] bool visible() const noexcept;

  private:
    class WindowClassRegistration;
    static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam,
                                             LPARAM lparam) noexcept;
    LRESULT handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    void populate(const wowai::storage::AssistantSettings& settings) noexcept;
    [[nodiscard]] bool collect(wowai::storage::AssistantSettings& settings) noexcept;

    HINSTANCE instance_{};
    HWND owner_{};
    std::unique_ptr<WindowClassRegistration> window_class_;
    HWND window_{};
    SaveHandler save_handler_;
    DeleteHandler delete_handler_;
};

} // namespace wowai::app
