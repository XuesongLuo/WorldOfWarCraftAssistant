#include "wowai/app/settings_window.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

namespace wowai::app {
namespace {

constexpr wchar_t settings_window_class[] = L"WorldOfWarcraftAssistant.Settings.Window.v1";
constexpr int control_hotkey_enabled = 101;
constexpr int control_ctrl = 102;
constexpr int control_alt = 103;
constexpr int control_shift = 104;
constexpr int control_win = 105;
constexpr int control_key = 106;
constexpr int control_opacity = 107;
constexpr int control_font_size = 108;
constexpr int control_history = 109;
constexpr int command_save = 201;
constexpr int command_defaults = 202;
constexpr int command_delete_data = 203;

[[noreturn]] void throw_last_error(const char* operation) {
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), operation);
}

HWND add_control(const HWND parent, const wchar_t* class_name, const wchar_t* text,
                 const DWORD style, const int x, const int y, const int width, const int height,
                 const int identifier) {
    return ::CreateWindowExW(
        0, class_name, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)), nullptr, nullptr);
}

void set_checked(const HWND window, const int identifier, const bool checked) noexcept {
    ::CheckDlgButton(window, identifier, checked ? BST_CHECKED : BST_UNCHECKED);
}

bool checked(const HWND window, const int identifier) noexcept {
    return ::IsDlgButtonChecked(window, identifier) == BST_CHECKED;
}

void set_number(const HWND window, const int identifier, const std::uint32_t value) noexcept {
    ::SetDlgItemTextW(window, identifier, std::to_wstring(value).c_str());
}

std::optional<std::uint32_t> get_number(const HWND window, const int identifier) noexcept {
    wchar_t text[16]{};
    if (::GetDlgItemTextW(window, identifier, text, static_cast<int>(std::size(text))) <= 0) {
        return std::nullopt;
    }
    try {
        std::size_t parsed{};
        const std::wstring value{text};
        const auto number = std::stoul(value, &parsed);
        return parsed == value.size() ? std::optional<std::uint32_t>{number} : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

class SettingsWindow::WindowClassRegistration final {
  public:
    explicit WindowClassRegistration(const HINSTANCE instance) : instance_(instance) {
        WNDCLASSEXW value{};
        value.cbSize = sizeof(value);
        value.lpfnWndProc = &SettingsWindow::window_procedure;
        value.hInstance = instance_;
        value.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
        value.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        value.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        value.lpszClassName = settings_window_class;
        atom_ = ::RegisterClassExW(&value);
        if (atom_ == 0) {
            throw_last_error("RegisterClassExW for settings failed");
        }
    }
    ~WindowClassRegistration() {
        if (atom_ != 0) {
            ::UnregisterClassW(settings_window_class, instance_);
        }
    }

  private:
    HINSTANCE instance_{};
    ATOM atom_{};
};

SettingsWindow::SettingsWindow(const HINSTANCE instance, const HWND owner, SaveHandler save_handler,
                               DeleteHandler delete_handler)
    : instance_(instance), owner_(owner),
      window_class_(std::make_unique<WindowClassRegistration>(instance)),
      save_handler_(std::move(save_handler)), delete_handler_(std::move(delete_handler)) {
    window_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, settings_window_class, L"助手设置",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                                CW_USEDEFAULT, 500, 430, owner_, nullptr, instance_, this);
    if (window_ == nullptr) {
        throw_last_error("CreateWindowExW for settings failed");
    }
}

SettingsWindow::~SettingsWindow() {
    if (window_ != nullptr) {
        ::DestroyWindow(window_);
        window_ = nullptr;
    }
    window_class_.reset();
}

void SettingsWindow::show(const wowai::storage::AssistantSettings& settings) noexcept {
    populate(settings);
    ::ShowWindow(window_, SW_SHOWNORMAL);
    ::SetForegroundWindow(window_);
}

bool SettingsWindow::visible() const noexcept {
    return window_ != nullptr && ::IsWindowVisible(window_) != FALSE;
}

LRESULT CALLBACK SettingsWindow::window_procedure(const HWND window, const UINT message,
                                                  const WPARAM wparam,
                                                  const LPARAM lparam) noexcept {
    auto* self = reinterpret_cast<SettingsWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self =
            static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handle_message(window, message, wparam, lparam)
                : ::DefWindowProcW(window, message, wparam, lparam);
}

LRESULT SettingsWindow::handle_message(const HWND window, const UINT message, const WPARAM wparam,
                                       const LPARAM lparam) noexcept {
    if (message == WM_CREATE) {
        add_control(window, L"STATIC", L"全局快捷键", 0, 24, 20, 180, 22, 0);
        add_control(window, L"BUTTON", L"启用", BS_AUTOCHECKBOX, 24, 48, 72, 24,
                    control_hotkey_enabled);
        add_control(window, L"BUTTON", L"Ctrl", BS_AUTOCHECKBOX, 104, 48, 62, 24, control_ctrl);
        add_control(window, L"BUTTON", L"Alt", BS_AUTOCHECKBOX, 170, 48, 58, 24, control_alt);
        add_control(window, L"BUTTON", L"Shift", BS_AUTOCHECKBOX, 232, 48, 66, 24, control_shift);
        add_control(window, L"BUTTON", L"Win", BS_AUTOCHECKBOX, 302, 48, 58, 24, control_win);
        const HWND combo = add_control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 368,
                                       46, 92, 260, control_key);
        ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Space"));
        for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
            wchar_t label[2]{letter, L'\0'};
            ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        }
        for (int index = 1; index <= 12; ++index) {
            const std::wstring label = L"F" + std::to_wstring(index);
            ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        add_control(window, L"STATIC", L"覆盖层透明度（20–100）", 0, 24, 100, 230, 22, 0);
        add_control(window, L"EDIT", L"", WS_BORDER | ES_NUMBER, 280, 96, 90, 26, control_opacity);
        add_control(window, L"STATIC", L"字体大小（12–28 px）", 0, 24, 140, 230, 22, 0);
        add_control(window, L"EDIT", L"", WS_BORDER | ES_NUMBER, 280, 136, 90, 26,
                    control_font_size);
        add_control(window, L"BUTTON", L"保存完整问题与回复（默认关闭）", BS_AUTOCHECKBOX, 24, 184,
                    340, 26, control_history);
        add_control(window, L"STATIC",
                    L"关闭后不会写入新会话，并立即清除已保存会话。API 密钥不会写入数据库。", 0, 24,
                    216, 440, 44, 0);
        add_control(window, L"BUTTON", L"保存", BS_DEFPUSHBUTTON, 24, 286, 92, 30, command_save);
        add_control(window, L"BUTTON", L"恢复默认", 0, 126, 286, 92, 30, command_defaults);
        add_control(window, L"BUTTON", L"删除我的本地数据…", 0, 228, 286, 166, 30,
                    command_delete_data);
        add_control(window, L"BUTTON", L"关闭", 0, 404, 286, 60, 30, IDCANCEL);
        return 0;
    }
    if (message == WM_COMMAND) {
        const auto command = LOWORD(wparam);
        if (command == command_save) {
            wowai::storage::AssistantSettings settings;
            if (!collect(settings)) {
                ::MessageBoxW(window, L"设置无效。快捷键必须含修饰键，数值也必须在范围内。",
                              L"无法保存", MB_OK | MB_ICONWARNING);
            } else if (save_handler_ && save_handler_(settings)) {
                ::ShowWindow(window, SW_HIDE);
            }
            return 0;
        }
        if (command == command_defaults) {
            populate(wowai::storage::AssistantSettings::defaults());
            return 0;
        }
        if (command == command_delete_data &&
            ::MessageBoxW(window, L"将删除会话、设置、受保护凭据、临时截图和诊断日志。是否继续？",
                          L"删除我的本地数据",
                          MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDYES) {
            if (delete_handler_ && delete_handler_()) {
                populate(wowai::storage::AssistantSettings::defaults());
                ::MessageBoxW(window, L"本地数据已删除，应用已恢复安全默认值。", L"删除完成",
                              MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
        if (command == IDCANCEL) {
            ::ShowWindow(window, SW_HIDE);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        ::ShowWindow(window, SW_HIDE);
        return 0;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

void SettingsWindow::populate(const wowai::storage::AssistantSettings& settings) noexcept {
    set_checked(window_, control_hotkey_enabled, settings.hotkey_enabled);
    set_checked(window_, control_ctrl, (settings.hotkey_modifiers & MOD_CONTROL) != 0);
    set_checked(window_, control_alt, (settings.hotkey_modifiers & MOD_ALT) != 0);
    set_checked(window_, control_shift, (settings.hotkey_modifiers & MOD_SHIFT) != 0);
    set_checked(window_, control_win, (settings.hotkey_modifiers & MOD_WIN) != 0);
    int key_index{};
    if (settings.hotkey_virtual_key >= 'A' && settings.hotkey_virtual_key <= 'Z') {
        key_index = 1 + static_cast<int>(settings.hotkey_virtual_key - 'A');
    } else if (settings.hotkey_virtual_key >= VK_F1 && settings.hotkey_virtual_key <= VK_F12) {
        key_index = 27 + static_cast<int>(settings.hotkey_virtual_key - VK_F1);
    }
    ::SendDlgItemMessageW(window_, control_key, CB_SETCURSEL, key_index, 0);
    set_number(window_, control_opacity, settings.overlay_opacity_percent);
    set_number(window_, control_font_size, settings.overlay_font_size_px);
    set_checked(window_, control_history, settings.save_conversation_history);
}

bool SettingsWindow::collect(wowai::storage::AssistantSettings& settings) noexcept {
    settings.hotkey_enabled = checked(window_, control_hotkey_enabled);
    settings.hotkey_modifiers = MOD_NOREPEAT;
    if (checked(window_, control_ctrl))
        settings.hotkey_modifiers |= MOD_CONTROL;
    if (checked(window_, control_alt))
        settings.hotkey_modifiers |= MOD_ALT;
    if (checked(window_, control_shift))
        settings.hotkey_modifiers |= MOD_SHIFT;
    if (checked(window_, control_win))
        settings.hotkey_modifiers |= MOD_WIN;
    const auto key_index =
        static_cast<int>(::SendDlgItemMessageW(window_, control_key, CB_GETCURSEL, 0, 0));
    if (key_index == 0) {
        settings.hotkey_virtual_key = VK_SPACE;
    } else if (key_index >= 1 && key_index <= 26) {
        settings.hotkey_virtual_key = static_cast<std::uint32_t>('A' + key_index - 1);
    } else if (key_index >= 27 && key_index <= 38) {
        settings.hotkey_virtual_key = VK_F1 + static_cast<std::uint32_t>(key_index - 27);
    } else {
        return false;
    }
    const auto opacity = get_number(window_, control_opacity);
    const auto font_size = get_number(window_, control_font_size);
    if (!opacity || !font_size) {
        return false;
    }
    settings.overlay_opacity_percent = *opacity;
    settings.overlay_font_size_px = *font_size;
    settings.save_conversation_history = checked(window_, control_history);
    return settings.valid();
}

} // namespace wowai::app
