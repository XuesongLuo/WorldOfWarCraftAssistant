#include "wowai/app/settings_window.hpp"
#include "wowai/overlay/overlay_window.hpp"
#include "wowai/platform/resources.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

#include <windows.h>

namespace {

constexpr wchar_t target_class[] = L"WorldOfWarcraftAssistant.Overlay.ComponentTarget";

LRESULT CALLBACK target_procedure(const HWND window, const UINT message, const WPARAM wparam,
                                  const LPARAM lparam) {
    if (message == WM_CLOSE) {
        ::DestroyWindow(window);
        return 0;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

int main() {
    try {
        auto apartment = wowai::platform::initialize_sta();
        const HINSTANCE instance = ::GetModuleHandleW(nullptr);
        WNDCLASSEXW registration{};
        registration.cbSize = sizeof(registration);
        registration.hInstance = instance;
        registration.lpfnWndProc = target_procedure;
        registration.lpszClassName = target_class;
        if (::RegisterClassExW(&registration) == 0) {
            return 10;
        }
        const HWND target = ::CreateWindowExW(0, target_class, L"Overlay component target",
                                              WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 960, 720,
                                              nullptr, nullptr, instance, nullptr);
        if (target == nullptr) {
            return 11;
        }
        ::SetForegroundWindow(target);
        const bool interactive_desktop = ::GetForegroundWindow() != nullptr;

        bool reported = false;
        std::wstring last_status;
        wowai::overlay::OverlayWindow overlay{instance,
                                              [&reported, &last_status](std::wstring status) {
                                                  reported = true;
                                                  last_status = std::move(status);
                                              }};
        overlay.set_target(target);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        MSG message{};
        while (!overlay.initialized() && std::chrono::steady_clock::now() < deadline) {
            while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
            }
            overlay.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const bool headless_controller_ready =
            !interactive_desktop &&
            last_status ==
                L"WebView2 composition controller initialized; waiting for the overlay document.";
        if ((!overlay.initialized() && !headless_controller_ready) || !reported) {
            std::wcerr << L"WebView2 composition overlay did not initialize: " << last_status
                       << L'\n';
            return 12;
        }
        const HWND overlay_window = ::FindWindowW(wowai::overlay::overlay_window_class, nullptr);
        if (overlay_window == nullptr) {
            return 13;
        }
        overlay.set_interaction_enabled(false);
        if (overlay.interaction_enabled()) {
            return 14;
        }
        const LONG_PTR pass_through_style = ::GetWindowLongPtrW(overlay_window, GWL_EXSTYLE);
        if ((pass_through_style & (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) !=
            (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) {
            return 17;
        }
        if (::SendMessageW(overlay_window, WM_NCHITTEST, 0, MAKELPARAM(1, 1)) != HTTRANSPARENT) {
            return 16;
        }
        overlay.set_interaction_enabled(true);
        if (!overlay.interaction_enabled()) {
            return 15;
        }
        if ((::GetWindowLongPtrW(overlay_window, GWL_EXSTYLE) &
             (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) != 0) {
            return 18;
        }
        overlay.apply_appearance(80, 20);
        overlay.set_user_visible(false);
        if (overlay.user_visible() || ::IsWindowVisible(overlay_window) != FALSE) {
            return 19;
        }
        overlay.set_user_visible(true);
        if (!overlay.user_visible()) {
            return 21;
        }
        wowai::app::SettingsWindow settings{
            instance, target,
            [](const wowai::storage::AssistantSettings&, const std::optional<std::string>&) {
                return true;
            },
            [] { return true; }, [](std::string_view, std::string_view) { return true; }};
        settings.show(wowai::storage::AssistantSettings::defaults());
        if (!settings.visible()) {
            return 22;
        }
        const HWND settings_handle =
            ::FindWindowW(L"WorldOfWarcraftAssistant.Settings.Window.v1", nullptr);
        if (settings_handle == nullptr) {
            return 23;
        }
        ::SendMessageW(settings_handle, WM_CLOSE, 0, 0);
        if (settings.visible()) {
            return 24;
        }
        // Queue cross-thread-style UI updates immediately before teardown. Overlay destruction must
        // reclaim them without dispatching into a destroyed WebView/window.
        overlay.post_request_state("cancelling", "正在取消请求…");
        overlay.post_assistant_message("late result must be safely owned until teardown");
        ::DestroyWindow(target);
        ::UnregisterClassW(target_class, instance);
        std::cout << (headless_controller_ready
                          ? "WebView2 composition controller test passed; document rendering "
                            "requires an interactive desktop\n"
                          : "WebView2 composition overlay component test passed\n");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 20;
    }
}
