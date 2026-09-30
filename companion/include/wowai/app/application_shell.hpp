#pragma once

#include "wowai/app/lifecycle.hpp"
#include "wowai/platform/resources.hpp"

#include <memory>

#include <windows.h>

namespace wowai::app {

inline constexpr wchar_t application_window_class[] =
    L"WorldOfWarcraftAssistant.Companion.Window.v1";
inline constexpr wchar_t single_instance_mutex[] = L"Local\\WorldOfWarcraftAssistant.Companion.v1";
inline constexpr UINT activate_existing_instance_message = WM_APP + 1;

class ApplicationShell final {
  public:
    explicit ApplicationShell(HINSTANCE instance);
    ~ApplicationShell();

    ApplicationShell(const ApplicationShell&) = delete;
    ApplicationShell& operator=(const ApplicationShell&) = delete;

    [[nodiscard]] int run();

  private:
    class WindowClassRegistration;
    class TrayIcon;

    static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam,
                                             LPARAM lparam) noexcept;
    [[nodiscard]] LRESULT handle_message(HWND window, UINT message, WPARAM wparam,
                                         LPARAM lparam) noexcept;
    void activate() noexcept;
    void show_tray_menu() noexcept;

    HINSTANCE instance_{};
    std::unique_ptr<WindowClassRegistration> window_class_;
    wowai::platform::UniqueWindow window_;
    std::unique_ptr<TrayIcon> tray_icon_;
    Lifecycle lifecycle_;
};

} // namespace wowai::app
