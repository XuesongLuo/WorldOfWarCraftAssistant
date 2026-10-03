#include "wowai/app/application_shell.hpp"

#include "wowai/app/build_info.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

#include <objbase.h>
#include <shellapi.h>

namespace wowai::app {
namespace {

constexpr UINT tray_callback_message = WM_APP + 2;
constexpr UINT command_exit = 1001;
constexpr UINT command_refresh_windows = 1002;
constexpr UINT command_manual_calibration = 1003;
constexpr UINT command_reset_calibration = 1004;
constexpr UINT command_forget_selection = 1005;
constexpr UINT command_toggle_interaction = 1006;
constexpr UINT command_settings = 1007;
constexpr UINT command_toggle_overlay = 1008;
constexpr UINT command_first_candidate = 2000;
constexpr UINT maximum_candidate_commands = 100;
constexpr UINT_PTR overlay_timer = 1;

[[noreturn]] void throw_last_error(const char* operation) {
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), operation);
}

std::string new_uuid_text() {
    GUID value{};
    if (FAILED(::CoCreateGuid(&value))) {
        throw std::runtime_error("could not create vision identifier");
    }
    std::array<char, 37> text{};
    const int written = std::snprintf(
        text.data(), text.size(), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", value.Data1,
        value.Data2, value.Data3, value.Data4[0], value.Data4[1], value.Data4[2], value.Data4[3],
        value.Data4[4], value.Data4[5], value.Data4[6], value.Data4[7]);
    if (written != 36) {
        throw std::runtime_error("could not format vision identifier");
    }
    return text.data();
}

std::string utc_now_text() {
    SYSTEMTIME value{};
    ::GetSystemTime(&value);
    std::array<char, 25> text{};
    const int written = std::snprintf(
        text.data(), text.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", value.wYear, value.wMonth,
        value.wDay, value.wHour, value.wMinute, value.wSecond, value.wMilliseconds);
    if (written != 24) {
        throw std::runtime_error("could not format vision timestamp");
    }
    return text.data();
}

bool development_environment_fallback_enabled() noexcept {
    wchar_t value[2]{};
    return ::GetEnvironmentVariableW(L"WOWAI_DEVELOPMENT_ENV_FALLBACK", value, 2) == 1 &&
           value[0] == L'1';
}

std::string utc_unix_text(const std::uint32_t timestamp) {
    const std::time_t value = static_cast<std::time_t>(timestamp);
    std::tm utc{};
    if (::gmtime_s(&utc, &value) != 0) {
        throw std::runtime_error("could not convert visual bridge timestamp");
    }
    std::array<char, 21> text{};
    const auto written = std::strftime(text.data(), text.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
    if (written != 20) {
        throw std::runtime_error("could not format visual bridge timestamp");
    }
    return text.data();
}

struct ActionableError {
    std::string text;
    std::string action;
};

ActionableError actionable_error(const std::string_view code, const bool recovered) {
    if (code == "AI_CREDENTIALS_MISSING") {
        return {"缺少当前云端提供方/配置档的 API 凭据。",
                "在设置中保存 API key；.env.local 仅供开发回退"};
    }
    if (code == "AI_AUTH_FAILED") {
        return {"云端提供方拒绝了当前凭据。", "检查 API key 是否有效且属于当前提供方"};
    }
    if (code == "AI_MODEL_UNAVAILABLE") {
        return {"配置的精确模型当前不可用。", "在设置中检查精确模型/部署名后重试"};
    }
    if (code == "AI_RATE_LIMITED") {
        return {"云端提供方正在限流，本次请求未自动重发。", "稍候点击重试"};
    }
    if (code == "AI_NETWORK_UNAVAILABLE") {
        return {"无法连接云端提供方，本次请求未自动重发。", "检查网络后点击重试"};
    }
    if (code == "AI_TIMEOUT") {
        return {"请求已超时并停止，本次请求未自动重发。", "确认网络稳定后点击重试"};
    }
    if (code == "AI_INVALID_RESPONSE") {
        return {"模型回复结构无效，未向聊天区显示不可信的部分内容。", "点击重试或更换模型"};
    }
    if (code == "AI_USAGE_LIMIT_REACHED") {
        return {"已达到月度云端请求硬上限，本次请求未发送。",
                "在设置中审查费用风险后调整月度上限"};
    }
    if (code == "AI_DUPLICATE_REQUEST_BLOCKED") {
        return {"已阻止重复请求以避免重复计费。", "无需重试；如需新问题请重新提交"};
    }
    if (code == "CODEX_START_FAILED" || code == "CODEX_PROTOCOL_ERROR") {
        return {recovered ? "Host/App Server 异常已安全恢复，本次请求未自动重发。"
                          : "Host/App Server 异常且恢复失败，本次请求未自动重发。",
                recovered ? "点击重试" : "检查 Host 配置后重启应用"};
    }
    if (code == "MODEL_CAPABILITY_MISSING") {
        return {"当前模型不支持这类已确认输入。", "更换支持该输入的精确模型"};
    }
    return {"请求失败，未显示不完整或未经验证的输出。", "检查配置后重试"};
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
        // Explorer can be unavailable during startup or component tests. The diagnostic window
        // remains a complete exit path and Explorer will be retried on the next application run.
        added_ = ::Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;

        if (added_) {
            data_.uVersion = NOTIFYICON_VERSION_4;
            ::Shell_NotifyIconW(NIM_SETVERSION, &data_);
        }
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

ApplicationShell::ApplicationShell(const HINSTANCE instance)
    : instance_(instance), paths_(wowai::storage::ApplicationPaths::defaults()) {
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

    bool storage_degraded = false;
    try {
        paths_.create_private_directories();
        local_data_cleaner_ = std::make_unique<wowai::storage::LocalDataCleaner>(paths_);
        static_cast<void>(local_data_cleaner_->clean_temporary_screenshots());
        database_ = std::make_unique<wowai::storage::LocalDatabase>(paths_.database);
    } catch (...) {
        storage_degraded = true;
        database_ =
            std::make_unique<wowai::storage::LocalDatabase>(std::filesystem::path{L":memory:"});
    }
    credential_store_ = std::make_unique<wowai::storage::CredentialStore>(paths_.credentials);
    try {
        safe_log_ = std::make_unique<wowai::storage::SafeLog>(paths_.logs);
        safe_log_->info("application.start");
    } catch (...) {
        storage_degraded = true;
    }
    settings_ = database_->load_settings();
    global_hotkey_ =
        std::make_unique<wowai::platform::GlobalHotkey>(window_.get(), hotkey_registrar_);

    tray_icon_ = std::make_unique<TrayIcon>(window_.get());
    lifecycle_.transition_to(LifecycleState::waiting_for_wow);
    selection_store_ = std::make_unique<wowai::capture::SelectionStore>(
        wowai::capture::SelectionStore::default_path());
    reload_assistant_session();
    overlay_window_ = std::make_unique<wowai::overlay::OverlayWindow>(
        instance_, [this](std::wstring status) { set_status(std::move(status)); },
        [this](wowai::overlay::WebMessage message) { handle_overlay_message(std::move(message)); });
    overlay_window_->apply_appearance(settings_.overlay_opacity_percent,
                                      settings_.overlay_font_size_px);
    settings_window_ = std::make_unique<SettingsWindow>(
        instance_, window_.get(),
        [this](const wowai::storage::AssistantSettings& value,
               const std::optional<std::string>& key) { return apply_settings(value, key); },
        [this] { return delete_local_data(); },
        [this](const std::string_view provider, const std::string_view profile) {
            return delete_cloud_credential(provider, profile);
        },
        [this](const wowai::storage::AssistantSettings& value,
               const std::optional<std::string>& key, const std::stop_token stop_token) {
            return test_cloud_connection(value, key, stop_token);
        });
    const bool hotkey_conflict =
        global_hotkey_->apply(settings_) == wowai::platform::HotkeyApplyResult::conflict;
    if (hotkey_conflict) {
        settings_.hotkey_enabled = false;
        database_->save_settings(settings_);
        safe_log_->error("hotkey.conflict");
    }
    if (::SetTimer(window_.get(), overlay_timer, 100, nullptr) == 0) {
        throw_last_error("SetTimer failed");
    }
    status_detail_ = L"Looking for World of Warcraft windows...";
    ::ShowWindow(window_.get(), SW_SHOWDEFAULT);
    ::UpdateWindow(window_.get());
    refresh_wow_windows();
    if (storage_degraded) {
        set_status(L"Local data storage is unavailable. Safe in-memory defaults are active; "
                   L"settings, credentials, sessions, and logs will not be written. Check "
                   L"LocalAppData permissions, then restart.");
    } else if (hotkey_conflict) {
        set_status(L"The configured global hotkey is already used by another application and was "
                   L"disabled. Open Settings from the tray menu to choose another shortcut.");
    }
}

ApplicationShell::~ApplicationShell() {
    if (window_) {
        ::KillTimer(window_.get(), overlay_timer);
    }
    if (request_thread_.joinable()) {
        request_thread_.request_stop();
        if (assistant_session_) {
            assistant_session_->cancel_active_request();
        }
        request_thread_.join();
    }
    discard_screenshot();
    global_hotkey_.reset();
    settings_window_.reset();
    assistant_session_.reset();
    overlay_window_.reset();
    tray_icon_.reset();
    if (safe_log_) {
        safe_log_->info("application.stop");
    }
    safe_log_.reset();
    credential_store_.reset();
    database_.reset();
    local_data_cleaner_.reset();
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
        if (LOWORD(wparam) == command_refresh_windows) {
            refresh_wow_windows();
            return 0;
        }
        if (LOWORD(wparam) == command_manual_calibration) {
            begin_manual_calibration();
            return 0;
        }
        if (LOWORD(wparam) == command_reset_calibration) {
            reset_calibration();
            return 0;
        }
        if (LOWORD(wparam) == command_forget_selection) {
            if (selection_store_) {
                selection_store_->clear();
            }
            refresh_wow_windows();
            return 0;
        }
        if (LOWORD(wparam) == command_toggle_interaction) {
            if (overlay_window_) {
                overlay_window_->toggle_interaction();
            }
            return 0;
        }
        if (LOWORD(wparam) == command_settings) {
            show_settings();
            return 0;
        }
        if (LOWORD(wparam) == command_toggle_overlay) {
            if (overlay_window_) {
                overlay_window_->toggle_visibility();
            }
            return 0;
        }
        if (LOWORD(wparam) >= command_first_candidate &&
            LOWORD(wparam) < command_first_candidate + maximum_candidate_commands) {
            select_candidate(LOWORD(wparam) - command_first_candidate, true);
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (wparam == wowai::platform::assistant_hotkey_id && overlay_window_) {
            overlay_window_->toggle_visibility();
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == overlay_timer && overlay_window_) {
            overlay_window_->tick();
            tick_observation();
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
            ::TextOutW(context, 24, 28, heading.c_str(), static_cast<int>(heading.size()));
            ::TextOutW(context, 24, 72, state_line.c_str(), static_cast<int>(state_line.size()));
            RECT detail_rect{24, 108, 490, 205};
            ::DrawTextW(context, status_detail_.c_str(), static_cast<int>(status_detail_.size()),
                        &detail_rect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
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

void ApplicationShell::show_settings() noexcept {
    if (settings_window_) {
        std::string suffix;
        try {
            auto secret =
                credential_store_->read(settings_.cloud_provider, settings_.cloud_profile);
            if (secret) {
                const auto start = secret->size() > 4 ? secret->size() - 4 : 0;
                suffix = secret->substr(start);
                ::SecureZeroMemory(secret->data(), secret->size());
            }
        } catch (...) {
        }
        settings_window_->show(settings_, std::move(suffix));
    }
}

bool ApplicationShell::apply_settings(const wowai::storage::AssistantSettings& settings,
                                      const std::optional<std::string>& api_key) noexcept {
    if (!settings.valid() || !database_ || !global_hotkey_) {
        return false;
    }
    if (request_active_) {
        ::MessageBoxW(window_.get(), L"请先等待当前请求完成或取消请求，再切换云端配置。",
                      L"请求进行中", MB_OK | MB_ICONWARNING);
        return false;
    }
    const auto result = global_hotkey_->apply(settings);
    if (result == wowai::platform::HotkeyApplyResult::conflict) {
        static_cast<void>(global_hotkey_->apply(settings_));
        ::MessageBoxW(settings_window_ && settings_window_->visible() ? ::GetForegroundWindow()
                                                                      : window_.get(),
                      L"该全局快捷键已被其他程序占用。原快捷键已恢复，请重新选择。", L"快捷键冲突",
                      MB_OK | MB_ICONWARNING);
        if (safe_log_)
            safe_log_->error("hotkey.conflict");
        return false;
    }
    try {
        if (api_key)
            credential_store_->write(settings.cloud_provider, settings.cloud_profile, *api_key);
        database_->save_settings(settings);
        settings_ = settings;
        cloud_session_requests_ = 0;
        reload_assistant_session();
        if (overlay_window_) {
            overlay_window_->apply_appearance(settings_.overlay_opacity_percent,
                                              settings_.overlay_font_size_px);
        }
        if (safe_log_)
            safe_log_->info("settings.saved");
        return true;
    } catch (...) {
        static_cast<void>(global_hotkey_->apply(settings_));
        ::MessageBoxW(window_.get(), L"本地设置无法安全保存，原设置仍然有效。", L"保存失败",
                      MB_OK | MB_ICONERROR);
        if (safe_log_)
            safe_log_->error("settings.save_failed");
        return false;
    }
}

bool ApplicationShell::delete_cloud_credential(const std::string_view provider,
                                               const std::string_view profile) noexcept {
    try {
        if (request_active_)
            return false;
        credential_store_->erase(provider, profile);
        if (settings_.cloud_provider == provider && settings_.cloud_profile == profile) {
            assistant_session_.reset();
            assistant_configuration_code_ = "AI_CREDENTIALS_MISSING";
            assistant_configuration_error_ = "当前提供方/配置档未保存 API key。";
        }
        if (safe_log_)
            safe_log_->info("credential.deleted", std::string{provider});
        return true;
    } catch (...) {
        if (safe_log_)
            safe_log_->error("credential.delete_failed");
        return false;
    }
}

std::string ApplicationShell::test_cloud_connection(
    const wowai::storage::AssistantSettings& settings,
    const std::optional<std::string>& api_key, const std::stop_token stop_token) {
    auto session = wowai::codex::AssistantSession::from_connection_test(
        settings, *credential_store_, api_key);
    if (!session)
        throw std::runtime_error("请先启用云端模型。");

    const std::string request_id = new_uuid_text();
    const auto authorization = database_->authorize_cloud_request(
        settings, 0, request_id, session->destination_host(), false);
    if (authorization == wowai::storage::CloudRequestAuthorization::duplicate)
        throw std::runtime_error("连接测试请求被幂等保护阻止。");
    if (authorization == wowai::storage::CloudRequestAuthorization::monthly_limit)
        throw std::runtime_error("已达到月度硬上限；未发送连接测试。");
    if (authorization != wowai::storage::CloudRequestAuthorization::allowed &&
        authorization != wowai::storage::CloudRequestAuthorization::allowed_with_warning)
        throw std::runtime_error("连接测试未获准发送。");

    static_cast<void>(session->ask("Connection test. Reply with OK only.",
                                   std::chrono::milliseconds{15'000}, std::nullopt,
                                   nlohmann::json::array(), nullptr, false, stop_token,
                                   request_id));
    return "真实连接成功。已发送固定短文本且仅请求最短回复；未发送截图、观察数据或玩家信息，"
           "也未自动重试。";
}

void ApplicationShell::reload_assistant_session() noexcept {
    assistant_session_.reset();
    assistant_configuration_code_.clear();
    assistant_configuration_error_.clear();
    try {
        assistant_session_ =
            wowai::codex::AssistantSession::from_secure_settings(settings_, *credential_store_);
    } catch (const wowai::codex::AssistantFailure& error) {
        if (settings_.cloud_enabled && development_environment_fallback_enabled()) {
            try {
                assistant_session_ = wowai::codex::AssistantSession::from_environment();
                if (assistant_session_)
                    return;
            } catch (...) {
            }
        }
        assistant_configuration_code_ = std::string{error.code()};
        assistant_configuration_error_ = error.what();
    } catch (...) {
        if (settings_.cloud_enabled && development_environment_fallback_enabled()) {
            try {
                assistant_session_ = wowai::codex::AssistantSession::from_environment();
                if (assistant_session_)
                    return;
            } catch (...) {
            }
        }
        assistant_configuration_code_ = "CODEX_START_FAILED";
        assistant_configuration_error_ = "Host 启动失败；请检查锁定运行时与路径配置。";
    }
}

bool ApplicationShell::delete_local_data() noexcept {
    try {
        cancel_request();
        if (request_thread_.joinable()) {
            request_thread_.join();
        }
        discard_screenshot();
        pause_observation();
        database_->reset_all();
        credential_store_->erase_all();
        assistant_session_.reset();
        cloud_session_requests_ = 0;
        safe_log_.reset();
        if (local_data_cleaner_) {
            static_cast<void>(local_data_cleaner_->delete_non_database_data());
        }
        paths_.create_private_directories();
        safe_log_ = std::make_unique<wowai::storage::SafeLog>(paths_.logs);
        settings_ = database_->load_settings();
        static_cast<void>(global_hotkey_->apply(settings_));
        if (overlay_window_) {
            overlay_window_->apply_appearance(settings_.overlay_opacity_percent,
                                              settings_.overlay_font_size_px);
        }
        safe_log_->info("local_data.deleted");
        return true;
    } catch (...) {
        if (!safe_log_) {
            try {
                safe_log_ = std::make_unique<wowai::storage::SafeLog>(paths_.logs);
            } catch (...) {
            }
        }
        if (safe_log_)
            safe_log_->error("local_data.delete_failed");
        ::MessageBoxW(window_.get(), L"部分本地数据正在使用或无法删除，请退出应用后重试。",
                      L"删除失败", MB_OK | MB_ICONERROR);
        return false;
    }
}

void ApplicationShell::show_tray_menu() noexcept {
    wowai::platform::UniqueMenu menu{::CreatePopupMenu()};
    if (!menu) {
        return;
    }
    if (::AppendMenuW(menu.get(), MF_STRING, command_refresh_windows, L"Refresh WoW windows") ==
        FALSE) {
        return;
    }
    ::AppendMenuW(menu.get(), MF_STRING, command_forget_selection, L"Forget saved WoW selection");
    ::AppendMenuW(menu.get(), MF_STRING, command_toggle_overlay,
                  overlay_window_ && overlay_window_->user_visible() ? L"Hide assistant overlay"
                                                                     : L"Show assistant overlay");
    ::AppendMenuW(menu.get(), MF_STRING, command_settings, L"Settings...");

    wowai::platform::UniqueMenu candidates_menu{::CreatePopupMenu()};
    if (candidates_menu) {
        const std::size_t count =
            std::min<std::size_t>(candidates_.size(), maximum_candidate_commands);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& candidate = candidates_[index];
            std::wstring label = candidate.identity.executable_name + L" (PID " +
                                 std::to_wstring(candidate.process_id) + L")";
            if (!candidate.title.empty()) {
                label += L" - " + candidate.title;
            }
            ::AppendMenuW(candidates_menu.get(), MF_STRING,
                          command_first_candidate + static_cast<UINT>(index), label.c_str());
        }
        if (count == 0) {
            ::AppendMenuW(candidates_menu.get(), MF_STRING | MF_GRAYED, 0, L"No clients found");
        }
        if (::AppendMenuW(menu.get(), MF_POPUP, reinterpret_cast<UINT_PTR>(candidates_menu.get()),
                          L"Select World of Warcraft") != FALSE) {
            candidates_menu.release();
        }
    }

    ::AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu.get(), selected_candidate_ ? MF_STRING : MF_STRING | MF_GRAYED,
                  command_manual_calibration, L"Calibrate overlay region...");
    ::AppendMenuW(menu.get(), manual_calibration_.configured() ? MF_STRING : MF_STRING | MF_GRAYED,
                  command_reset_calibration, L"Reset overlay region");
    ::AppendMenuW(menu.get(), selected_candidate_ ? MF_STRING : MF_STRING | MF_GRAYED,
                  command_toggle_interaction,
                  overlay_window_ && overlay_window_->interaction_enabled()
                      ? L"Enable mouse pass-through"
                      : L"Enable overlay interaction");
    ::AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
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

void ApplicationShell::refresh_wow_windows() noexcept {
    try {
        pause_observation();
        discard_screenshot();
        candidates_ = window_discovery_.discover();
        selected_candidate_.reset();
        content_rect_.reset();
        if (overlay_window_) {
            overlay_window_->clear_target();
        }
        lifecycle_.on_wow_exited();

        const auto preferred = selection_store_ ? selection_store_->load() : std::nullopt;
        const auto selection = wowai::capture::choose_window(candidates_, preferred);
        if (selection.kind == wowai::capture::SelectionKind::selected &&
            selection.candidate_index) {
            select_candidate(*selection.candidate_index, candidates_.size() == 1);
            return;
        }
        if (selection.kind == wowai::capture::SelectionKind::requires_user) {
            set_status(L"Multiple WoW clients found. Right-click the tray icon and select one; "
                       L"the assistant will not guess.");
        } else {
            set_status(L"No supported WoW window found. Start the retail client, then choose "
                       L"Refresh WoW windows from the tray icon.");
        }
    } catch (...) {
        set_status(L"WoW window discovery failed. No window was selected.");
    }
}

void ApplicationShell::select_candidate(const std::size_t index, const bool remember) noexcept {
    if (index >= candidates_.size() || ::IsWindow(candidates_[index].window) == FALSE) {
        set_status(L"The selected WoW window is no longer available. Refresh the window list.");
        return;
    }
    try {
        pause_observation();
        discard_screenshot();
        selected_candidate_ = candidates_[index];
        lifecycle_.on_wow_exited();
        lifecycle_.transition_to(LifecycleState::ready);
        if ((remember || candidates_.size() == 1) && selection_store_) {
            selection_store_->save(selected_candidate_->identity);
        }
        RECT client{};
        if (::GetClientRect(selected_candidate_->window, &client) == FALSE || client.right <= 0 ||
            client.bottom <= 0) {
            throw std::runtime_error("selected WoW client has no usable client rectangle");
        }
        content_rect_ = wowai::capture::Rect{0, 0, client.right, client.bottom};
        detect_anchor();
        if (overlay_window_) {
            overlay_window_->set_target(selected_candidate_->window, content_rect_);
        }
    } catch (...) {
        selected_candidate_.reset();
        lifecycle_.on_wow_exited();
        set_status(L"The selected WoW client could not be saved or inspected.");
    }
}

void ApplicationShell::detect_anchor() noexcept {
    if (!selected_candidate_) {
        return;
    }
    try {
        const auto frame = window_capture_.capture_client(selected_candidate_->window);
        if (const auto manual = manual_calibration_.resolve({frame.width, frame.height})) {
            content_rect_ = manual;
            set_status(L"Overlay ready without requiring the addon. Using the player-confirmed "
                       L"overlay region.");
            return;
        }
        const auto detection = anchor_detector_.detect(frame.view());
        if (!detection) {
            set_status(L"Overlay ready. The optional addon marker was not found, so the full WoW "
                       L"client region is being used.");
            return;
        }
        content_rect_ = detection->content_rect;
        set_status(L"Overlay ready. Optional addon marker found; enhanced region: " +
                   std::to_wstring(content_rect_->left) + L"," +
                   std::to_wstring(content_rect_->top) + L" - " +
                   std::to_wstring(content_rect_->right) + L"," +
                   std::to_wstring(content_rect_->bottom) + L". No image was saved.");
    } catch (...) {
        set_status(L"Overlay ready without addon context. The optional marker check could not "
                   L"capture the selected client.");
    }
}

void ApplicationShell::begin_manual_calibration() noexcept {
    if (!selected_candidate_ || ::IsWindow(selected_candidate_->window) == FALSE) {
        set_status(L"Select a live WoW window before manual calibration.");
        return;
    }

    ::MessageBoxW(window_.get(),
                  L"After clicking OK, move the pointer to the TOP-LEFT corner of the desired "
                  L"overlay region within three seconds.",
                  L"Overlay region calibration", MB_OK | MB_ICONINFORMATION);
    ::Sleep(3000);
    POINT top_left{};
    if (::GetCursorPos(&top_left) == FALSE ||
        ::ScreenToClient(selected_candidate_->window, &top_left) == FALSE) {
        set_status(L"Manual calibration could not read the top-left point.");
        return;
    }

    ::MessageBoxW(window_.get(),
                  L"After clicking OK, move the pointer to the BOTTOM-RIGHT corner of the desired "
                  L"overlay region within three seconds.",
                  L"Overlay region calibration", MB_OK | MB_ICONINFORMATION);
    ::Sleep(3000);
    POINT bottom_right{};
    RECT client{};
    if (::GetCursorPos(&bottom_right) == FALSE ||
        ::ScreenToClient(selected_candidate_->window, &bottom_right) == FALSE ||
        ::GetClientRect(selected_candidate_->window, &client) == FALSE ||
        !manual_calibration_.set({top_left.x, top_left.y, bottom_right.x, bottom_right.y},
                                 {client.right, client.bottom})) {
        set_status(L"Manual calibration was outside the selected WoW client and was discarded.");
        return;
    }

    content_rect_ = manual_calibration_.resolve({client.right, client.bottom});
    if (overlay_window_) {
        overlay_window_->set_target(selected_candidate_->window, content_rect_);
    }
    set_status(L"Overlay region calibration applied. It remains local to this running session.");
}

void ApplicationShell::reset_calibration() noexcept {
    manual_calibration_.reset();
    content_rect_.reset();
    set_status(L"Overlay region reset. Refreshing the optional addon marker...");
    if (selected_candidate_) {
        RECT client{};
        if (::GetClientRect(selected_candidate_->window, &client) != FALSE && client.right > 0 &&
            client.bottom > 0) {
            content_rect_ = wowai::capture::Rect{0, 0, client.right, client.bottom};
        }
        detect_anchor();
        if (overlay_window_) {
            overlay_window_->set_target(selected_candidate_->window, content_rect_);
        }
    }
}

void ApplicationShell::set_status(std::wstring detail) noexcept {
    status_detail_ = std::move(detail);
    if (window_) {
        ::InvalidateRect(window_.get(), nullptr, TRUE);
    }
}

void ApplicationShell::handle_overlay_message(wowai::overlay::WebMessage message) noexcept {
    switch (message.kind) {
    case wowai::overlay::WebMessageKind::send_message:
        submit_question(std::move(message.text));
        break;
    case wowai::overlay::WebMessageKind::cancel_request:
        cancel_request();
        break;
    case wowai::overlay::WebMessageKind::capture_screenshot:
        capture_screenshot(message.enabled, message.selected_region);
        break;
    case wowai::overlay::WebMessageKind::confirm_screenshot:
        confirm_screenshot();
        break;
    case wowai::overlay::WebMessageKind::discard_screenshot:
        discard_screenshot();
        break;
    case wowai::overlay::WebMessageKind::set_observation:
        start_observation(message.text == "coaching" ? wowai::capture::ObservationMode::coaching
                                                     : wowai::capture::ObservationMode::scene);
        break;
    case wowai::overlay::WebMessageKind::pause_observation:
        pause_observation();
        break;
    case wowai::overlay::WebMessageKind::ready:
    case wowai::overlay::WebMessageKind::set_interaction:
    case wowai::overlay::WebMessageKind::open_external:
        break;
    }
}

void ApplicationShell::capture_screenshot(const bool mask_chat,
                                          const bool selected_region) noexcept {
    if (!overlay_window_ || !selected_candidate_ ||
        ::IsWindow(selected_candidate_->window) == FALSE ||
        ::IsIconic(selected_candidate_->window) != FALSE) {
        if (overlay_window_) {
            overlay_window_->post_status("请先选择一个未最小化的 WoW 客户端。", true);
        }
        return;
    }
    try {
        auto frame = window_capture_.capture_client(selected_candidate_->window);
        std::string capture_scope = "wow-window";
        if (selected_region && manual_calibration_.configured() && content_rect_ &&
            content_rect_->valid()) {
            frame = wowai::capture::crop_image(frame.view(), *content_rect_);
            capture_scope = "selected-region";
        }
        bool privacy_mask_applied = false;
        if (mask_chat) {
            const std::array masks{
                wowai::capture::Rect{0, frame.height * 2 / 3, frame.width / 2, frame.height}};
            wowai::capture::apply_privacy_masks(frame, masks);
            privacy_mask_applied = true;
        }
        const auto analysis = wowai::capture::analyze_image(frame.view());
        if (analysis.validity != wowai::capture::ImageValidity::valid) {
            throw std::runtime_error("captured frame is empty, black, or too dark");
        }
        auto encoded = wowai::capture::encode_png_bounded(frame.view());
        const auto encoded_width = encoded.width;
        const auto encoded_height = encoded.height;
        pending_screenshot_ = PendingScreenshot{
            std::move(encoded),   encoded_width, encoded_height,           utc_now_text(),
            privacy_mask_applied, false,         std::move(capture_scope), {}};
        overlay_window_->post_screenshot_preview(
            pending_screenshot_->encoded.base64, pending_screenshot_->width,
            pending_screenshot_->height, pending_screenshot_->privacy_mask_applied);
    } catch (const std::exception&) {
        discard_screenshot();
        overlay_window_->post_status(
            "截图失败或画面为空/过暗。请使用窗口化全屏并确认所选 WoW 窗口可见。", true);
    }
}

void ApplicationShell::confirm_screenshot() noexcept {
    if (!pending_screenshot_ || !overlay_window_) {
        if (overlay_window_) {
            overlay_window_->post_status("没有可确认的截图。", true);
        }
        return;
    }
    pending_screenshot_->confirmed = true;
    pending_screenshot_->upload_confirmed_at = utc_now_text();
    const std::string destination =
        assistant_session_ ? std::string{assistant_session_->provider()} : "已配置的云端提供方";
    overlay_window_->post_status("截图已确认：将上传至 " + destination +
                                     "，仅用于下一次图片问题；丢弃或请求结束后立即释放。",
                                 false);
}

void ApplicationShell::discard_screenshot() noexcept {
    if (pending_screenshot_) {
        std::fill(pending_screenshot_->encoded.bytes.begin(),
                  pending_screenshot_->encoded.bytes.end(), 0);
        std::fill(pending_screenshot_->encoded.base64.begin(),
                  pending_screenshot_->encoded.base64.end(), '\0');
        pending_screenshot_.reset();
    }
    if (overlay_window_) {
        overlay_window_->clear_screenshot_preview();
    }
}

void ApplicationShell::start_observation(const wowai::capture::ObservationMode mode) noexcept {
    if (!selected_candidate_ || ::IsWindow(selected_candidate_->window) == FALSE ||
        !overlay_window_ || !overlay_window_->visible()) {
        if (overlay_window_) {
            overlay_window_->post_status("观察会话只能在所选 WoW 窗口和持续可见状态条下启动。",
                                         true);
        }
        return;
    }
    observation_session_.start(mode, std::chrono::steady_clock::now());
    overlay_window_->post_status(
        mode == wowai::capture::ObservationMode::coaching
            ? "实战教学观察已开启：仅本地、只读、无游戏输入；失焦后不会自动恢复。"
            : "场景感知已开启：低频、仅本地、原始帧不落盘；失焦后不会自动恢复。",
        false);
}

void ApplicationShell::pause_observation() noexcept {
    const bool was_active = observation_session_.active();
    observation_session_.pause();
    if (was_active && overlay_window_) {
        overlay_window_->post_status("观察已立即暂停，原始帧已释放；需要再次显式开启。", false);
    }
}

void ApplicationShell::tick_observation() noexcept {
    if (!observation_session_.active() || !selected_candidate_ || !overlay_window_) {
        return;
    }
    const HWND target = selected_candidate_->window;
    const HWND foreground = ::GetForegroundWindow();
    const wowai::capture::ObservationGate gate{
        ::IsWindow(target) != FALSE,
        foreground == target || overlay_window_->owns_foreground(),
        ::IsIconic(target) != FALSE,
        overlay_window_->visible(),
    };
    const auto decision = observation_session_.evaluate(gate, std::chrono::steady_clock::now());
    if (!decision.active) {
        overlay_window_->post_status(
            "观察已因 WoW 失焦、最小化、退出或状态条不可见而停止，不会自动恢复。", true);
        return;
    }
    if (decision.capture) {
        process_observation_frame();
    }
}

void ApplicationShell::process_observation_frame() noexcept {
    if (!selected_candidate_ || !overlay_window_) {
        return;
    }
    try {
        const auto frame = window_capture_.capture_client(selected_candidate_->window);
        const auto scene = wowai::capture::classify_scene(frame.view());
        const std::string captured_at = utc_now_text();
        const std::string scene_name{wowai::capture::to_string(scene.kind)};
        const std::string mode =
            observation_session_.mode() == wowai::capture::ObservationMode::coaching ? "coaching"
                                                                                     : "scene";
        const std::string reason = scene.kind == wowai::capture::SceneKind::unknown
                                       ? "低置信度结果按未知处理，不作为确定事实。"
                                       : "由所选 WoW 客户区的低频本地像素特征粗分类。";
        observations_.push_back({{"id", new_uuid_text()},
                                 {"source", "screen-observed"},
                                 {"kind", "game-state"},
                                 {"capturedAt", captured_at},
                                 {"confidence", scene.confidence},
                                 {"summary", "scene=" + scene_name + "; reason=" + reason}});
        while (observations_.size() > 32) {
            observations_.erase(observations_.begin());
        }
        overlay_window_->post_observation(mode, scene_name, captured_at, scene.confidence, reason);

        wowai::capture::Rect bridge_rect{};
        const auto bridge_bytes = wowai::capture::sample_visual_bridge(frame.view(), bridge_rect);
        if (bridge_bytes) {
            const auto decoded = wowai::capture::decode_visual_bridge_frame(*bridge_bytes);
            if (decoded.frame &&
                visual_bridge_gate_.accept(*decoded.frame,
                                           static_cast<std::uint32_t>(std::time(nullptr))) ==
                    wowai::capture::VisualBridgeError::none) {
                nlohmann::json allowed = nlohmann::json::array();
                for (const auto& [key, ignored] : decoded.frame->fields) {
                    static_cast<void>(ignored);
                    if (key != "unavailable") {
                        allowed.push_back(key);
                    }
                }
                visual_bridge_context_ = {
                    {"protocolVersion", decoded.frame->protocol_version},
                    {"source", "plugin-public"},
                    {"sequence", decoded.frame->sequence},
                    {"capturedAt", utc_unix_text(decoded.frame->captured_at_unix)},
                    {"confidence", 1.0},
                    {"allowedFields", allowed},
                    {"unavailableFields", decoded.frame->unavailable_fields},
                };
                observations_.push_back(
                    {{"id", new_uuid_text()},
                     {"source", "plugin-public"},
                     {"kind", "game-state"},
                     {"capturedAt", utc_unix_text(decoded.frame->captured_at_unix)},
                     {"confidence", 1.0},
                     {"summary", nlohmann::json(decoded.frame->fields).dump()}});
                while (observations_.size() > 32) {
                    observations_.erase(observations_.begin());
                }
            }
        }
        // frame owns the only raw observation pixels and is destroyed here. No image is logged,
        // persisted, or attached to a request by the continuous observation path.
    } catch (const std::exception&) {
        pause_observation();
        overlay_window_->post_status("观察捕获失败并已停止；没有保存、记录或上传原始帧。", true);
    }
}

void ApplicationShell::submit_question(std::string question) noexcept {
    if (!overlay_window_) {
        return;
    }
    if (!assistant_session_) {
        const std::string code = assistant_configuration_code_.empty()
                                     ? "MODEL_PROVIDER_UNAVAILABLE"
                                     : assistant_configuration_code_;
        const auto detail =
            assistant_configuration_error_.empty()
                ? ActionableError{"云端 Host 尚未配置。",
                                  "配置 Host、提供方、精确模型、上传同意和 .env.local 凭据后重启"}
                : actionable_error(code, false);
        overlay_window_->post_request_state("error", detail.text, code, false, detail.action);
        return;
    }
    if (request_active_.exchange(true)) {
        overlay_window_->post_status("已有云端请求正在处理，请等待完成。", true);
        return;
    }
    if (request_thread_.joinable()) {
        request_thread_.join();
    }
    std::optional<wowai::codex::ConfirmedImage> confirmed_image;
    if (pending_screenshot_ && pending_screenshot_->confirmed) {
        confirmed_image = wowai::codex::ConfirmedImage{new_uuid_text(),
                                                       pending_screenshot_->encoded.mime_type,
                                                       pending_screenshot_->capture_scope,
                                                       pending_screenshot_->encoded.sha256,
                                                       pending_screenshot_->encoded.base64,
                                                       pending_screenshot_->privacy_mask_applied,
                                                       pending_screenshot_->upload_confirmed_at};
    }
    nlohmann::json observations = nlohmann::json::array();
    for (const auto& observation : observations_) {
        if (observation.value("source", "") != "screen-observed") {
            observations.push_back(observation);
        }
    }
    const auto visual_bridge = visual_bridge_context_;
    // Continuous screen observations remain local. Only a separately confirmed screenshot may
    // cross the cloud boundary; plugin-public structured context is independently opt-in.
    const bool observation_enabled = false;
    const std::string request_id = new_uuid_text();
    bool usage_warning = false;
    try {
        const auto authorization = database_->authorize_cloud_request(
            settings_, cloud_session_requests_, request_id, assistant_session_->destination_host(),
            confirmed_image.has_value());
        if (authorization != wowai::storage::CloudRequestAuthorization::allowed &&
            authorization != wowai::storage::CloudRequestAuthorization::allowed_with_warning) {
            request_active_ = false;
            const bool duplicate =
                authorization == wowai::storage::CloudRequestAuthorization::duplicate;
            overlay_window_->post_request_state(
                "error",
                duplicate ? "已阻止重复请求，避免重复计费。"
                          : "已达到月度云端请求硬上限；本次请求未发送。",
                duplicate ? "AI_DUPLICATE_REQUEST_BLOCKED" : "AI_USAGE_LIMIT_REACHED", false,
                "在设置中检查月度硬上限；提高上限前请确认费用");
            return;
        }
        usage_warning =
            authorization == wowai::storage::CloudRequestAuthorization::allowed_with_warning;
        ++cloud_session_requests_;
        if (safe_log_) {
            safe_log_->info("cloud.request",
                            "provider=" + settings_.cloud_provider +
                                " model=" + settings_.cloud_model +
                                " destination=" + assistant_session_->destination_host() +
                                " image=" + (confirmed_image ? "1" : "0"));
        }
    } catch (...) {
        request_active_ = false;
        overlay_window_->post_request_state("error", "用量保护或审计不可用；请求已安全阻断。",
                                            "AI_USAGE_LIMIT_REACHED", false,
                                            "检查本地数据目录后重试");
        return;
    }
    if (confirmed_image) {
        discard_screenshot();
    }
    overlay_window_->post_request_state(
        "submitting", usage_warning ? "已达到会话/每日提醒值；仍按你的设置继续请求云端模型…"
                                    : "正在请求已启用的云端模型…");
    request_thread_ = std::jthread([this, question = std::move(question),
                                    image = std::move(confirmed_image), observations, visual_bridge,
                                    observation_enabled,
                                    request_id](const std::stop_token stop_token) mutable {
        try {
            const std::string answer = assistant_session_->ask(
                question, std::chrono::milliseconds{30'000}, std::move(image), observations,
                visual_bridge, observation_enabled, stop_token, request_id);
            if (database_) {
                try {
                    database_->save_exchange(question, answer);
                } catch (...) {
                    if (safe_log_)
                        safe_log_->error("conversation.save_failed");
                }
            }
            if (overlay_window_) {
                overlay_window_->post_assistant_message(answer);
                overlay_window_->post_request_state("completed", "云端回复完成");
            }
        } catch (const wowai::codex::AssistantRequestCancelled&) {
            if (overlay_window_) {
                overlay_window_->post_request_state("cancelled", "请求已取消；不会显示迟到回复。");
            }
        } catch (const wowai::codex::AssistantFailure& error) {
            bool recovered = false;
            if (error.host_recovery_recommended()) {
                try {
                    assistant_session_->recover();
                    recovered = true;
                } catch (...) {
                    recovered = false;
                }
            }
            if (overlay_window_) {
                const std::string code{error.code()};
                const auto detail = actionable_error(code, recovered);
                overlay_window_->post_request_state("error", detail.text, code,
                                                    error.retryable() || recovered, detail.action);
            }
        } catch (...) {
            if (overlay_window_) {
                overlay_window_->post_request_state(
                    "error", "请求失败，诊断信息已脱敏；本次请求未自动重发。", "CODEX_START_FAILED",
                    true, "点击重试；若再次失败请重启应用");
            }
        }
        request_active_ = false;
    });
}

void ApplicationShell::cancel_request() noexcept {
    if (!request_active_.load() || !assistant_session_ || !request_thread_.joinable()) {
        if (overlay_window_) {
            overlay_window_->post_status("当前没有可取消的请求。", false);
        }
        return;
    }
    if (overlay_window_) {
        overlay_window_->post_request_state("cancelling", "正在取消请求…");
    }
    request_thread_.request_stop();
    assistant_session_->cancel_active_request();
}

} // namespace wowai::app
