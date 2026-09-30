#include "wowai/app/application_shell.hpp"

#include "wowai/app/build_info.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <system_error>

#include <shellapi.h>

namespace wowai::app {
namespace {

constexpr UINT tray_callback_message = WM_APP + 2;
constexpr UINT command_exit = 1001;

[[noreturn]] void throw_last_error(const char* operation) {
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), operation);
}

} // namespace

class ApplicationShell::WindowClassRegistration final {
  public:
    explicit WindowClassRegistration(const HINSTANCE instance) : instance_(instance) {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &ApplicationShell::window_procedure;
        window_class.hInstance = instance_;
        window_class.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        window_class.lpszClassName = application_window_class;
        window_class.hIconSm = window_class.hIcon;
        atom_ = ::RegisterClassExW(&window_class);
        if (atom_ == 0) {
            throw_last_error("RegisterClassExW failed");
        }
    }

    ~WindowClassRegistration() {
        if (atom_ != 0) {
            ::UnregisterClassW(application_window_class, instance_);
        }
    }

    WindowClassRegistration(const WindowClassRegistration&) = delete;
    WindowClassRegistration& operator=(const WindowClassRegistration&) = delete;

  private:
    HINSTANCE instance_{};
    ATOM atom_{};
};

class ApplicationShell::TrayIcon final {
  public:
    explicit TrayIcon(const HWND window) {
        data_.cbSize = sizeof(data_);
        data_.hWnd = window;
        data_.uID = 1;
        data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data_.uCallbackMessage = tray_callback_message;
        data_.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
        constexpr wchar_t tooltip[] = L"World of Warcraft AI Assistant";
        static_assert(std::size(tooltip) <= std::size(data_.szTip));
        ::wcscpy_s(data_.szTip, tooltip);
        if (::Shell_NotifyIconW(NIM_ADD, &data_) == FALSE) {
            throw_last_error("Shell_NotifyIconW(NIM_ADD) failed");
        }
        added_ = true;

        data_.uVersion = NOTIFYICON_VERSION_4;
        ::Shell_NotifyIconW(NIM_SETVERSION, &data_);
    }

    ~TrayIcon() {
        if (added_) {
            ::Shell_NotifyIconW(NIM_DELETE, &data_);
        }
    }

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

  private:
    NOTIFYICONDATAW data_{};
    bool added_{false};
};

ApplicationShell::ApplicationShell(const HINSTANCE instance) : instance_(instance) {
    if (instance_ == nullptr) {
        throw std::invalid_argument("application instance must not be null");
    }

    window_class_ = std::make_unique<WindowClassRegistration>(instance_);
    window_.reset(::CreateWindowExW(0, application_window_class, L"World of Warcraft AI Assistant",
                                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 520, 240,
                                    nullptr, nullptr, instance_, this));
    if (!window_) {
        throw_last_error("CreateWindowExW failed");
    }

    tray_icon_ = std::make_unique<TrayIcon>(window_.get());
    lifecycle_.transition_to(LifecycleState::waiting_for_wow);
    ::ShowWindow(window_.get(), SW_SHOWDEFAULT);
    ::UpdateWindow(window_.get());
}

ApplicationShell::~ApplicationShell() {
    tray_icon_.reset();
    window_.reset();
    window_class_.reset();
}

int ApplicationShell::run() {
    MSG message{};
    while (true) {
        const BOOL result = ::GetMessageW(&message, nullptr, 0, 0);
        if (result == 0) {
            return static_cast<int>(message.wParam);
        }
        if (result == -1) {
            throw_last_error("GetMessageW failed");
        }
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

LRESULT CALLBACK ApplicationShell::window_procedure(const HWND window, const UINT message,
                                                    const WPARAM wparam,
                                                    const LPARAM lparam) noexcept {
    ApplicationShell* self =
        reinterpret_cast<ApplicationShell*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<ApplicationShell*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self != nullptr) {
        return self->handle_message(window, message, wparam, lparam);
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

LRESULT ApplicationShell::handle_message(const HWND window, const UINT message, const WPARAM wparam,
                                         const LPARAM lparam) noexcept {
    switch (message) {
    case activate_existing_instance_message:
        activate();
        return 1;
    case tray_callback_message:
        if (LOWORD(lparam) == WM_CONTEXTMENU || LOWORD(lparam) == WM_RBUTTONUP) {
            show_tray_menu();
        } else if (LOWORD(lparam) == WM_LBUTTONDBLCLK) {
            activate();
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == command_exit) {
            ::DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        ::DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        tray_icon_.reset();
        if (window_.get() == window) {
            window_.release();
        }
        ::PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC context = ::BeginPaint(window, &paint);
        if (context != nullptr) {
            const std::wstring heading = L"World of Warcraft AI Assistant";
            const std::string_view state = to_string(lifecycle_.state());
            const std::wstring state_line = L"Status: " + std::wstring(state.begin(), state.end());
            const std::wstring detail =
                L"Waiting for World of Warcraft. Use the tray icon to exit.";
            ::TextOutW(context, 24, 28, heading.c_str(), static_cast<int>(heading.size()));
            ::TextOutW(context, 24, 72, state_line.c_str(), static_cast<int>(state_line.size()));
            ::TextOutW(context, 24, 108, detail.c_str(), static_cast<int>(detail.size()));
            ::EndPaint(window, &paint);
        }
        return 0;
    }
    default:
        break;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

void ApplicationShell::activate() noexcept {
    if (!window_) {
        return;
    }
    if (::IsIconic(window_.get()) != FALSE) {
        ::ShowWindow(window_.get(), SW_RESTORE);
    } else {
        ::ShowWindow(window_.get(), SW_SHOW);
    }
    ::SetForegroundWindow(window_.get());
    ::FlashWindow(window_.get(), TRUE);
}

void ApplicationShell::show_tray_menu() noexcept {
    wowai::platform::UniqueMenu menu{::CreatePopupMenu()};
    if (!menu) {
        return;
    }
    if (::AppendMenuW(menu.get(), MF_STRING, command_exit, L"Exit") == FALSE) {
        return;
    }

    POINT cursor{};
    if (::GetCursorPos(&cursor) == FALSE) {
        return;
    }
    ::SetForegroundWindow(window_.get());
    ::TrackPopupMenu(menu.get(), TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursor.x,
                     cursor.y, 0, window_.get(), nullptr);
}

} // namespace wowai::app
