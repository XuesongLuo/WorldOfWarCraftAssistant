#include "wowai/app/settings_window.hpp"

#include <algorithm>
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
constexpr int control_cloud_enabled = 110;
constexpr int control_provider = 111;
constexpr int control_connection = 112;
constexpr int control_model = 113;
constexpr int control_profile = 114;
constexpr int control_organization = 115;
constexpr int control_region = 116;
constexpr int control_resource_label = 117;
constexpr int control_resource = 118;
constexpr int control_api_version = 119;
constexpr int control_api_key = 120;
constexpr int control_credential_status = 121;
constexpr int control_session_limit = 122;
constexpr int control_daily_limit = 123;
constexpr int control_monthly_limit = 124;
constexpr int command_save = 201;
constexpr int command_defaults = 202;
constexpr int command_delete_data = 203;
constexpr int command_delete_credential = 204;
constexpr int command_test_configuration = 205;
constexpr UINT connection_test_complete_message = WM_APP + 1;

struct ConnectionTestResult {
    bool succeeded{};
    std::string message;
};

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

void set_ascii(const HWND window, const int identifier, const std::string& value) noexcept {
    std::wstring text(value.begin(), value.end());
    ::SetDlgItemTextW(window, identifier, text.c_str());
}

std::optional<std::string> get_ascii(const HWND window, const int identifier,
                                     const std::size_t maximum) noexcept {
    std::wstring text(maximum + 2, L'\0');
    const int written =
        ::GetDlgItemTextW(window, identifier, text.data(), static_cast<int>(text.size()));
    if (written < 0 || static_cast<std::size_t>(written) > maximum)
        return std::nullopt;
    text.resize(static_cast<std::size_t>(written));
    std::string result;
    result.reserve(text.size());
    for (const wchar_t character : text) {
        if (character < 0x20 || character > 0x7e)
            return std::nullopt;
        result.push_back(static_cast<char>(character));
    }
    return result;
}

std::wstring wide_from_utf8(const std::string_view value) {
    if (value.empty())
        return {};
    const int size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        return L"连接测试失败。";
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string selected_provider(const HWND window) {
    constexpr std::array providers{"openai",     "deepseek",  "xai",
                                   "openrouter", "dashscope", "azure-openai"};
    const auto index = static_cast<std::size_t>(
        std::max<LRESULT>(0, ::SendDlgItemMessageW(window, control_provider, CB_GETCURSEL, 0, 0)));
    return index < providers.size() ? providers[index] : "openai";
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
                               DeleteHandler delete_handler,
                               DeleteCredentialHandler delete_credential_handler,
                               ConnectionTestHandler connection_test_handler)
    : instance_(instance), owner_(owner),
      window_class_(std::make_unique<WindowClassRegistration>(instance)),
      save_handler_(std::move(save_handler)), delete_handler_(std::move(delete_handler)),
      delete_credential_handler_(std::move(delete_credential_handler)),
      connection_test_handler_(std::move(connection_test_handler)) {
    window_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, settings_window_class, L"助手设置",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                                CW_USEDEFAULT, 720, 760, owner_, nullptr, instance_, this);
    if (window_ == nullptr) {
        throw_last_error("CreateWindowExW for settings failed");
    }
}

SettingsWindow::~SettingsWindow() {
    if (connection_test_thread_.joinable()) {
        connection_test_thread_.request_stop();
        connection_test_thread_.join();
    }
    if (window_ != nullptr) {
        ::DestroyWindow(window_);
        window_ = nullptr;
    }
    window_class_.reset();
}

void SettingsWindow::show(const wowai::storage::AssistantSettings& settings,
                          std::string credential_suffix) noexcept {
    credential_suffix_ = std::move(credential_suffix);
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
                    216, 650, 22, 0);

        add_control(window, L"BUTTON", L"启用云端模型（关闭时零外发）", BS_AUTOCHECKBOX, 24, 246,
                    300, 24, control_cloud_enabled);
        add_control(window, L"STATIC", L"提供方", 0, 24, 280, 74, 22, 0);
        const HWND provider = add_control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                                          104, 276, 190, 180, control_provider);
        for (const wchar_t* label : {L"OpenAI", L"DeepSeek", L"xAI", L"OpenRouter",
                                     L"阿里云 DashScope/Qwen", L"Azure OpenAI"}) {
            ::SendMessageW(provider, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        }
        add_control(window, L"STATIC", L"", 0, 312, 278, 370, 44, control_connection);
        add_control(window, L"STATIC", L"精确模型/部署", 0, 24, 326, 108, 22, 0);
        add_control(window, L"EDIT", L"", WS_BORDER, 140, 322, 250, 26, control_model);
        add_control(window, L"STATIC", L"配置档", 0, 410, 326, 58, 22, 0);
        add_control(window, L"EDIT", L"default", WS_BORDER, 470, 322, 132, 26, control_profile);
        add_control(window, L"STATIC", L"Organization（OpenAI，可选）", 0, 24, 364, 208, 22, 0);
        add_control(window, L"EDIT", L"", WS_BORDER, 238, 360, 180, 26, control_organization);
        add_control(window, L"STATIC", L"区域", 0, 438, 364, 42, 22, 0);
        const HWND region = add_control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                                        484, 360, 126, 180, control_region);
        for (const wchar_t* label :
             {L"singapore", L"beijing", L"hongkong", L"tokyo", L"frankfurt", L"virginia"}) {
            ::SendMessageW(region, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        }
        add_control(window, L"STATIC", L"资源/Workspace", 0, 24, 402, 118, 22,
                    control_resource_label);
        add_control(window, L"EDIT", L"", WS_BORDER, 146, 398, 226, 26, control_resource);
        add_control(window, L"STATIC", L"API version", 0, 390, 402, 82, 22, 0);
        add_control(window, L"EDIT", L"v1", WS_BORDER, 478, 398, 132, 26, control_api_version);
        add_control(window, L"STATIC", L"API Key", 0, 24, 440, 70, 22, 0);
        add_control(window, L"EDIT", L"", WS_BORDER | ES_PASSWORD, 100, 436, 272, 26,
                    control_api_key);
        add_control(window, L"STATIC", L"未配置", 0, 386, 440, 154, 22, control_credential_status);
        add_control(window, L"BUTTON", L"删除凭据", 0, 548, 434, 82, 28, command_delete_credential);

        add_control(window, L"STATIC", L"提醒：会话（0 关闭）", 0, 24, 480, 142, 22, 0);
        add_control(window, L"EDIT", L"", WS_BORDER | ES_NUMBER, 170, 476, 50, 26,
                    control_session_limit);
        add_control(window, L"STATIC", L"每日提醒（0 关闭）", 0, 234, 480, 132, 22, 0);
        add_control(window, L"EDIT", L"", WS_BORDER | ES_NUMBER, 370, 476, 62, 26,
                    control_daily_limit);
        add_control(window, L"STATIC", L"月度硬上限", 0, 452, 480, 88, 22, 0);
        add_control(window, L"EDIT", L"", WS_BORDER | ES_NUMBER, 544, 476, 80, 26,
                    control_monthly_limit);
        add_control(window, L"STATIC",
                    L"截图/问题会发送到上方显示的目的域并可能产生费用；失败后图片绝不自动重发。"
                    L" Anthropic/Gemini/Mistral 原生协议不受锁定 App Server 支持，当前不启用。",
                    0, 24, 516, 650, 48, 0);
        add_control(window, L"BUTTON", L"测试真实连接（最小请求）", 0, 24, 574, 250, 30,
                    command_test_configuration);

        add_control(window, L"BUTTON", L"保存", BS_DEFPUSHBUTTON, 24, 646, 92, 30, command_save);
        add_control(window, L"BUTTON", L"恢复默认", 0, 126, 646, 92, 30, command_defaults);
        add_control(window, L"BUTTON", L"删除我的本地数据…", 0, 228, 646, 166, 30,
                    command_delete_data);
        add_control(window, L"BUTTON", L"关闭", 0, 610, 646, 60, 30, IDCANCEL);
        return 0;
    }
    if (message == WM_COMMAND) {
        const auto command = LOWORD(wparam);
        if (command == command_save) {
            wowai::storage::AssistantSettings settings;
            const auto key = get_ascii(window, control_api_key, 16 * 1024);
            if (!collect(settings)) {
                ::MessageBoxW(window, L"设置无效。请检查快捷键、云端必填字段和用量上限。",
                              L"无法保存", MB_OK | MB_ICONWARNING);
            } else if (!key) {
                ::MessageBoxW(window, L"API Key 只能包含可打印字符且长度不得超过 16 KiB。",
                              L"无法保存", MB_OK | MB_ICONWARNING);
            } else if (save_handler_ &&
                       save_handler_(settings, key->empty() ? std::nullopt
                                                            : std::optional<std::string>{*key})) {
                ::SetDlgItemTextW(window, control_api_key, L"");
                ::ShowWindow(window, SW_HIDE);
            }
            return 0;
        }
        if (command == command_defaults) {
            populate(wowai::storage::AssistantSettings::defaults());
            return 0;
        }
        if (command == command_delete_credential) {
            const auto profile = get_ascii(window, control_profile, 32);
            const auto provider = selected_provider(window);
            if (profile && delete_credential_handler_ &&
                ::MessageBoxW(window, L"删除当前提供方/配置档的受保护 API Key？", L"删除凭据",
                              MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDYES &&
                delete_credential_handler_(provider, *profile)) {
                credential_suffix_.clear();
                ::SetDlgItemTextW(window, control_credential_status, L"未配置");
            }
            return 0;
        }
        if (command == command_test_configuration) {
            wowai::storage::AssistantSettings candidate;
            const auto key = get_ascii(window, control_api_key, 16 * 1024);
            if (!collect(candidate) || !key) {
                ::MessageBoxW(window, L"配置或 API Key 格式校验失败；未发出网络请求。",
                              L"真实连接测试",
                              MB_OK | MB_ICONWARNING);
            } else if (!connection_test_active_ && connection_test_handler_ &&
                       ::MessageBoxW(
                           window,
                           L"将向当前提供方发送一条固定短文本，不含截图、观察数据或玩家信息；"
                           L"限制为最小回复且不会自动重试。该请求可能产生极小费用，并计入月度上限。"
                           L"是否继续？",
                           L"测试真实连接", MB_YESNO | MB_DEFBUTTON2 | MB_ICONINFORMATION) ==
                           IDYES) {
                if (connection_test_thread_.joinable())
                    connection_test_thread_.join();
                connection_test_active_ = true;
                update_cloud_controls();
                ::SetDlgItemTextW(window, command_test_configuration, L"正在测试…");
                connection_test_thread_ = std::jthread(
                    [this, candidate, key = key->empty() ? std::nullopt
                                                         : std::optional<std::string>{*key},
                     window](const std::stop_token stop_token) {
                        auto result = std::make_unique<ConnectionTestResult>();
                        try {
                            result->message = connection_test_handler_(candidate, key, stop_token);
                            result->succeeded = true;
                        } catch (const std::exception& error) {
                            result->message = error.what();
                        } catch (...) {
                            result->message = "连接测试失败；没有自动重试。";
                        }
                        if (!::PostMessageW(window, connection_test_complete_message, 0,
                                            reinterpret_cast<LPARAM>(result.get()))) {
                            return;
                        }
                        static_cast<void>(result.release());
                    });
            }
            return 0;
        }
        if (command == control_provider && HIWORD(wparam) == CBN_SELCHANGE) {
            update_cloud_controls();
            return 0;
        }
        if (command == control_cloud_enabled && HIWORD(wparam) == BN_CLICKED) {
            update_cloud_controls();
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
    if (message == connection_test_complete_message) {
        std::unique_ptr<ConnectionTestResult> result{
            reinterpret_cast<ConnectionTestResult*>(lparam)};
        connection_test_active_ = false;
        ::SetDlgItemTextW(window, command_test_configuration, L"测试真实连接（最小请求）");
        update_cloud_controls();
        const auto detail = wide_from_utf8(result ? result->message : "连接测试失败。");
        ::MessageBoxW(window, detail.c_str(), L"真实连接测试",
                      MB_OK | (result && result->succeeded ? MB_ICONINFORMATION : MB_ICONERROR));
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
    set_checked(window_, control_cloud_enabled, settings.cloud_enabled);
    constexpr std::array providers{"openai",     "deepseek",  "xai",
                                   "openrouter", "dashscope", "azure-openai"};
    const auto provider = std::find(providers.begin(), providers.end(), settings.cloud_provider);
    ::SendDlgItemMessageW(
        window_, control_provider, CB_SETCURSEL,
        provider == providers.end() ? 0 : std::distance(providers.begin(), provider), 0);
    set_ascii(window_, control_model, settings.cloud_model);
    set_ascii(window_, control_profile, settings.cloud_profile);
    set_ascii(window_, control_organization, settings.cloud_organization);
    constexpr std::array regions{"singapore", "beijing",   "hongkong",
                                 "tokyo",     "frankfurt", "virginia"};
    const auto region = std::find(regions.begin(), regions.end(), settings.cloud_region);
    ::SendDlgItemMessageW(window_, control_region, CB_SETCURSEL,
                          region == regions.end() ? 0 : std::distance(regions.begin(), region), 0);
    set_ascii(window_, control_resource, settings.cloud_resource);
    set_ascii(window_, control_api_version, settings.cloud_api_version);
    set_number(window_, control_session_limit, settings.cloud_session_request_limit);
    set_number(window_, control_daily_limit, settings.cloud_daily_request_limit);
    set_number(window_, control_monthly_limit, settings.cloud_monthly_request_limit);
    ::SetDlgItemTextW(window_, control_api_key, L"");
    const std::wstring status =
        credential_suffix_.empty()
            ? L"未配置"
            : L"已配置（末尾 ****" +
                  std::wstring(credential_suffix_.begin(), credential_suffix_.end()) + L"）";
    ::SetDlgItemTextW(window_, control_credential_status, status.c_str());
    update_cloud_controls();
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
    settings.cloud_enabled = checked(window_, control_cloud_enabled);
    settings.cloud_provider = selected_provider(window_);
    const auto model = get_ascii(window_, control_model, 128);
    const auto profile = get_ascii(window_, control_profile, 32);
    const auto organization = get_ascii(window_, control_organization, 128);
    const auto resource = get_ascii(window_, control_resource, 64);
    const auto api_version = get_ascii(window_, control_api_version, 32);
    constexpr std::array regions{"singapore", "beijing",   "hongkong",
                                 "tokyo",     "frankfurt", "virginia"};
    const auto region_index = static_cast<std::size_t>(
        std::max<LRESULT>(0, ::SendDlgItemMessageW(window_, control_region, CB_GETCURSEL, 0, 0)));
    const auto session_limit = get_number(window_, control_session_limit);
    const auto daily_limit = get_number(window_, control_daily_limit);
    const auto monthly_limit = get_number(window_, control_monthly_limit);
    if (!model || !profile || !organization || !resource || !api_version ||
        region_index >= regions.size() || !session_limit || !daily_limit || !monthly_limit)
        return false;
    settings.cloud_model = *model;
    settings.cloud_profile = *profile;
    settings.cloud_organization = *organization;
    settings.cloud_region = regions[region_index];
    settings.cloud_resource = *resource;
    settings.cloud_api_version = *api_version;
    settings.cloud_session_request_limit = *session_limit;
    settings.cloud_daily_request_limit = *daily_limit;
    settings.cloud_monthly_request_limit = *monthly_limit;
    return settings.valid();
}

void SettingsWindow::update_cloud_controls() noexcept {
    const std::string provider = selected_provider(window_);
    const bool enabled = checked(window_, control_cloud_enabled);
    const bool dashscope = provider == "dashscope";
    const bool azure = provider == "azure-openai";
    for (const int control : {control_provider, control_model, control_profile, control_api_key,
                              control_session_limit, control_daily_limit, control_monthly_limit,
                              command_test_configuration}) {
        ::EnableWindow(::GetDlgItem(window_, control), enabled && !connection_test_active_);
    }
    ::EnableWindow(::GetDlgItem(window_, control_organization), enabled && provider == "openai");
    ::EnableWindow(::GetDlgItem(window_, control_region), enabled && dashscope);
    ::EnableWindow(::GetDlgItem(window_, control_resource), enabled && (dashscope || azure));
    ::EnableWindow(::GetDlgItem(window_, control_api_version), enabled && azure);
    ::SetDlgItemTextW(window_, control_resource_label,
                      dashscope ? L"Workspace ID"
                      : azure   ? L"Azure 资源名"
                                : L"预设 endpoint");
    const wchar_t* description = L"云端已关闭：不会启动 Host/App Server，也不会联网";
    if (enabled && provider == "openai")
        description = L"原生 Responses → api.openai.com · 图片按模型白名单";
    else if (enabled && provider == "deepseek")
        description = L"兼容 Responses → api.deepseek.com · 图片按模型白名单";
    else if (enabled && provider == "xai")
        description = L"兼容 Responses → api.x.ai · 图片按模型白名单";
    else if (enabled && provider == "openrouter")
        description = L"OpenResponses → openrouter.ai（图片关闭）";
    else if (enabled && provider == "dashscope")
        description = L"兼容 Responses → 区域 maas.aliyuncs.com · 图片按模型白名单";
    else if (enabled && provider == "azure-openai")
        description = L"部署式 Responses → *.openai.azure.com（图片关闭）";
    ::SetDlgItemTextW(window_, control_connection, description);
}

} // namespace wowai::app
