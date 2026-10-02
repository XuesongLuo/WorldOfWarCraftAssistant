#include "wowai/app/application_shell.hpp"

#include "wowai/app/build_info.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <system_error>

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
constexpr UINT command_first_candidate = 2000;
constexpr UINT maximum_candidate_commands = 100;
constexpr UINT_PTR overlay_timer = 1;

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
    selection_store_ = std::make_unique<wowai::capture::SelectionStore>(
        wowai::capture::SelectionStore::default_path());
    try {
        assistant_session_ = wowai::codex::AssistantSession::from_environment();
    } catch (const std::exception& error) {
        assistant_configuration_error_ = error.what();
    }
    overlay_window_ = std::make_unique<wowai::overlay::OverlayWindow>(
        instance_, [this](std::wstring status) { set_status(std::move(status)); },
        [this](std::string question) { submit_question(std::move(question)); });
    if (::SetTimer(window_.get(), overlay_timer, 100, nullptr) == 0) {
        throw_last_error("SetTimer failed");
    }
    status_detail_ = L"Looking for World of Warcraft windows...";
    ::ShowWindow(window_.get(), SW_SHOWDEFAULT);
    ::UpdateWindow(window_.get());
    refresh_wow_windows();
}

ApplicationShell::~ApplicationShell() {
    if (window_) {
        ::KillTimer(window_.get(), overlay_timer);
    }
    if (request_thread_.joinable()) {
        request_thread_.join();
    }
    assistant_session_.reset();
    overlay_window_.reset();
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
        if (LOWORD(wparam) >= command_first_candidate &&
            LOWORD(wparam) < command_first_candidate + maximum_candidate_commands) {
            select_candidate(LOWORD(wparam) - command_first_candidate, true);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == overlay_timer && overlay_window_) {
            overlay_window_->tick();
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

void ApplicationShell::submit_question(std::string question) noexcept {
    if (!overlay_window_) {
        return;
    }
    if (!assistant_session_) {
        std::string detail = assistant_configuration_error_.empty()
                                 ? "本地 Host 尚未配置。请设置 WOWAI_NODE_BINARY、"
                                   "WOWAI_HOST_SCRIPT、WOWAI_MODEL_PROVIDER、"
                                   "WOWAI_LOCAL_MODEL_ENDPOINT、WOWAI_LOCAL_MODEL 以及 "
                                   "WOWAI_CODEX_BINARY/LOCK/ROOT。"
                                 : "本地 Host 配置无效：" + assistant_configuration_error_;
        overlay_window_->post_status(std::move(detail), true);
        return;
    }
    if (request_active_.exchange(true)) {
        overlay_window_->post_status("已有本地请求正在处理，请等待完成。", true);
        return;
    }
    if (request_thread_.joinable()) {
        request_thread_.join();
    }
    request_thread_ = std::jthread([this, question = std::move(question)] {
        try {
            const std::string answer =
                assistant_session_->ask(question, std::chrono::milliseconds{30'000});
            if (overlay_window_) {
                overlay_window_->post_assistant_message(answer);
            }
        } catch (const std::exception& error) {
            if (overlay_window_) {
                overlay_window_->post_status(
                    "本地模型请求失败：" + std::string{error.what()}, true);
            }
        }
        request_active_ = false;
    });
}

} // namespace wowai::app
