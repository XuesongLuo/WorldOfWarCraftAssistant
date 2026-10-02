#include "wowai/overlay/overlay_window.hpp"

#include "wowai/overlay/overlay_policy.hpp"
#include "wowai/overlay/web_message_bridge.hpp"
#include "wowai/platform/resources.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <WebView2.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <windowsx.h>
#include <wrl.h>

namespace wowai::overlay {
namespace {

using Microsoft::WRL::Callback;

constexpr wchar_t overlay_title[] = L"World of Warcraft AI Assistant Overlay";
constexpr wchar_t overlay_host[] = L"wowai-overlay.invalid";
constexpr wchar_t overlay_uri[] = L"https://wowai-overlay.invalid/index.html";

constexpr wchar_t chat_html[] = LR"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<style>
:root{color-scheme:dark;font:16px/1.45 "Segoe UI",sans-serif}*{box-sizing:border-box}
html,body{width:100%;height:100%;margin:0;background:transparent;color:#f4f1e8}
body{padding:10px}.panel{height:100%;display:grid;grid-template-rows:auto 1fr auto;overflow:hidden;
background:rgba(12,16,24,.92);border:1px solid rgba(218,174,92,.8);border-radius:14px;
box-shadow:0 12px 36px rgba(0,0,0,.45)}header{display:flex;align-items:center;gap:8px;padding:12px 14px;
border-bottom:1px solid rgba(255,255,255,.12)}h1{font-size:15px;margin:0;flex:1;color:#ffd98a}
#mode{font-size:12px;color:#b9c2d0}button{font:inherit;color:inherit;background:#28364c;border:1px solid #526783;
border-radius:8px;padding:7px 10px}button:hover,button:focus-visible{background:#354864;outline:2px solid #ffd98a}
#messages{overflow:auto;padding:14px;scrollbar-color:#65758a transparent}.message{margin:0 0 12px;padding:10px 12px;
border-radius:10px;white-space:pre-wrap;overflow-wrap:anywhere}.assistant{background:#1d2939}.user{background:#46371f}
.message a{color:#ffd98a}
.status{font-size:13px;color:#bac5d3;padding:6px 14px 0}form{display:grid;grid-template-columns:1fr auto;gap:8px;padding:10px 14px 14px}
textarea{resize:none;min-height:46px;max-height:120px;color:#fff;background:#111a27;border:1px solid #526783;
border-radius:8px;padding:10px;font:inherit}textarea:focus{outline:2px solid #ffd98a}#send{background:#8a5b16}
@media (prefers-contrast:more){.panel,button,textarea{border-width:2px}.panel{background:#000}}
</style></head><body><main class="panel" aria-label="魔兽世界 AI 助手聊天">
<header><h1>WoW AI 助手 · 本地 PoC</h1><span id="mode">交互模式</span>
<button id="pass" type="button" title="切换后鼠标将穿透覆盖层">鼠标穿透</button></header>
<section id="messages" role="log" aria-live="polite"><p class="message assistant">覆盖层已独立运行。配置本地 Ollama 与 Codex Host 后即可进行纯本地文字问答。<a href="https://support.blizzard.com/">暴雪支持</a></p></section>
<div id="status" class="status" role="status">等待输入</div>
<form id="form"><textarea id="input" maxlength="4000" aria-label="问题" placeholder="输入问题…"></textarea>
<button id="send" type="submit">发送</button></form></main>
<script>
const bridge=(type,payload={})=>chrome.webview.postMessage(JSON.stringify({version:1,type,payload}));
const messages=document.querySelector('#messages'),input=document.querySelector('#input'),status=document.querySelector('#status');
function append(kind,text){const p=document.createElement('p');p.className='message '+kind;p.textContent=text;messages.append(p);messages.scrollTop=messages.scrollHeight}
document.querySelector('#form').addEventListener('submit',e=>{e.preventDefault();const text=input.value.trim();if(!text){status.textContent='请输入内容';input.focus();return}append('user',text);bridge('send_message',{text});input.value='';status.textContent='正在请求本地模型…'});
input.addEventListener('keydown',e=>{if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();document.querySelector('#form').requestSubmit()}});
document.querySelector('#pass').addEventListener('click',()=>bridge('set_interaction',{enabled:false}));
document.addEventListener('click',e=>{const link=e.target.closest('a[href]');if(!link)return;e.preventDefault();bridge('open_external',{url:link.href})});
chrome.webview.addEventListener('message',e=>{const m=e.data;if(!m||m.version!==1)return;if(m.type==='assistant_message'){append('assistant',m.text);status.textContent='本地回复完成'}else if(m.type==='status'){status.textContent=m.text}});
bridge('ready');
</script></body></html>)HTML";

[[noreturn]] void throw_last_error(const char* operation) {
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), operation);
}

[[nodiscard]] std::string utf8_from_wide(const std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int size =
        ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                              static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        throw_last_error("WideCharToMultiByte failed");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                              static_cast<int>(value.size()), result.data(), size, nullptr,
                              nullptr) != size) {
        throw_last_error("WideCharToMultiByte failed");
    }
    return result;
}

[[nodiscard]] std::filesystem::path webview_data_path() {
    std::wstring module_path(32768, L'\0');
    const DWORD module_length =
        ::GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (module_length == 0 || module_length >= module_path.size()) {
        throw_last_error("GetModuleFileNameW failed");
    }
    module_path.resize(module_length);
    const std::wstring profile_name = std::filesystem::path{module_path}.stem().wstring();

    wil::unique_cotaskmem_string local_app_data;
    THROW_IF_FAILED(
        ::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local_app_data));
    const std::filesystem::path path = std::filesystem::path{local_app_data.get()} /
                                       L"WorldOfWarcraftAssistant" / L"WebView2" / profile_name;
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (!error) {
        return path;
    }

    const std::filesystem::path fallback = std::filesystem::temp_directory_path() /
                                           L"WorldOfWarcraftAssistant" / L"WebView2" / profile_name;
    error.clear();
    std::filesystem::create_directories(fallback, error);
    if (error) {
        throw std::filesystem::filesystem_error("create WebView2 user data directory", fallback,
                                                error);
    }
    return fallback;
}

[[nodiscard]] std::filesystem::path
write_chat_ui_asset(const std::filesystem::path& webview_profile_path) {
    const std::filesystem::path ui_path = webview_profile_path.parent_path().parent_path() /
                                          L"OverlayUi" / webview_profile_path.filename();
    std::error_code error;
    std::filesystem::create_directories(ui_path, error);
    if (error) {
        throw std::filesystem::filesystem_error("create overlay UI directory", ui_path, error);
    }
    const std::string utf8_html = utf8_from_wide(chat_html);
    std::ofstream output{ui_path / L"index.html", std::ios::binary | std::ios::trunc};
    output.write(utf8_html.data(), static_cast<std::streamsize>(utf8_html.size()));
    output.close();
    if (!output) {
        throw std::runtime_error("write overlay UI asset failed");
    }
    return ui_path;
}

[[nodiscard]] COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS mouse_keys(const WPARAM wparam) noexcept {
    unsigned int keys = COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE;
    if ((wparam & MK_LBUTTON) != 0) {
        keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON;
    }
    if ((wparam & MK_RBUTTON) != 0) {
        keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_RIGHT_BUTTON;
    }
    if ((wparam & MK_MBUTTON) != 0) {
        keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_MIDDLE_BUTTON;
    }
    if ((wparam & MK_SHIFT) != 0) {
        keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_SHIFT;
    }
    if ((wparam & MK_CONTROL) != 0) {
        keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_CONTROL;
    }
    return static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(keys);
}

void apply_interaction_style(const HWND window, const bool enabled) noexcept {
    LONG_PTR style = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (enabled) {
        style &= ~(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    } else {
        // Microsoft documents that top-level WS_EX_TRANSPARENT windows need
        // WS_EX_LAYERED for cross-process hit-test transparency.
        style |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    }
    ::SetWindowLongPtrW(window, GWL_EXSTYLE, style);
    if (!enabled) {
        ::SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
    }
    ::SetWindowPos(window, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

} // namespace

struct OverlayWindow::State final {
    HWND window{};
    HWND target{};
    std::optional<wowai::capture::Rect> content_rect;
    StatusSink status_sink;
    MessageSink message_sink;
    bool interaction_enabled{true};
    bool webview_initialized{};
    bool document_ready{};
    bool shutting_down{};
    bool tracking_mouse{};
    wowai::platform::ComPtr<ID3D11Device> d3d_device;
    wowai::platform::ComPtr<IDCompositionDevice> composition_device;
    wowai::platform::ComPtr<IDCompositionTarget> composition_target;
    wowai::platform::ComPtr<IDCompositionVisual> root_visual;
    wowai::platform::ComPtr<ICoreWebView2Environment> environment;
    wowai::platform::ComPtr<ICoreWebView2Controller> controller;
    wowai::platform::ComPtr<ICoreWebView2CompositionController> composition_controller;
    wowai::platform::ComPtr<ICoreWebView2> webview;
    std::string document_source;

    void report(std::wstring message) noexcept {
        try {
            if (status_sink) {
                status_sink(std::move(message));
            }
        } catch (...) {
        }
    }

    void post_json(const std::string& json) noexcept {
        if (!webview) {
            return;
        }
        try {
            const int count = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, json.data(),
                                                    static_cast<int>(json.size()), nullptr, 0);
            if (count <= 0) {
                return;
            }
            std::wstring wide(static_cast<std::size_t>(count), L'\0');
            if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, json.data(),
                                      static_cast<int>(json.size()), wide.data(), count) == count) {
                webview->PostWebMessageAsJson(wide.c_str());
            }
        } catch (...) {
        }
    }
};

class OverlayWindow::WindowClassRegistration final {
  public:
    explicit WindowClassRegistration(const HINSTANCE instance) : instance_(instance) {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &OverlayWindow::window_procedure;
        window_class.hInstance = instance_;
        window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = overlay_window_class;
        atom_ = ::RegisterClassExW(&window_class);
        if (atom_ == 0) {
            throw_last_error("RegisterClassExW for overlay failed");
        }
    }

    ~WindowClassRegistration() {
        if (atom_ != 0) {
            ::UnregisterClassW(overlay_window_class, instance_);
        }
    }

  private:
    HINSTANCE instance_{};
    ATOM atom_{};
};

OverlayWindow::OverlayWindow(const HINSTANCE instance, StatusSink status_sink,
                             MessageSink message_sink)
    : window_class_(std::make_unique<WindowClassRegistration>(instance)),
      state_(std::make_shared<State>()) {
    state_->status_sink = std::move(status_sink);
    state_->message_sink = std::move(message_sink);
    const DWORD extended_style = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP;
    state_->window =
        ::CreateWindowExW(extended_style, overlay_window_class, overlay_title, WS_POPUP, 0, 0, 440,
                          560, nullptr, nullptr, instance, state_.get());
    if (state_->window == nullptr) {
        throw_last_error("CreateWindowExW for overlay failed");
    }

    D3D_FEATURE_LEVEL feature_level{};
    THROW_IF_FAILED(::D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
        D3D11_SDK_VERSION, state_->d3d_device.put(), &feature_level, nullptr));
    wowai::platform::ComPtr<IDXGIDevice> dxgi_device;
    THROW_IF_FAILED(state_->d3d_device->QueryInterface(IID_PPV_ARGS(dxgi_device.put())));
    THROW_IF_FAILED(::DCompositionCreateDevice(dxgi_device.get(),
                                               IID_PPV_ARGS(state_->composition_device.put())));
    THROW_IF_FAILED(state_->composition_device->CreateTargetForHwnd(
        state_->window, TRUE, state_->composition_target.put()));
    THROW_IF_FAILED(state_->composition_device->CreateVisual(state_->root_visual.put()));
    THROW_IF_FAILED(state_->composition_target->SetRoot(state_->root_visual.get()));
    THROW_IF_FAILED(state_->composition_device->Commit());

    const std::filesystem::path data_path = webview_data_path();
    const std::filesystem::path ui_path = write_chat_ui_asset(data_path);
    const std::weak_ptr<State> weak_state = state_;
    const HRESULT create_result = ::CreateCoreWebView2EnvironmentWithOptions(
        nullptr, data_path.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak_state, ui_path](const HRESULT error,
                                  ICoreWebView2Environment* environment) -> HRESULT {
                const auto state = weak_state.lock();
                if (!state || state->shutting_down) {
                    return S_OK;
                }
                if (FAILED(error) || environment == nullptr) {
                    state->report(L"WebView2 Runtime is unavailable. Repair or install the "
                                  L"Evergreen Runtime, then restart the assistant.");
                    return S_OK;
                }
                state->environment = environment;
                wowai::platform::ComPtr<ICoreWebView2Environment3> environment3;
                if (FAILED(environment->QueryInterface(IID_PPV_ARGS(environment3.put())))) {
                    state->report(L"The installed WebView2 Runtime does not support composition.");
                    return S_OK;
                }
                return environment3->CreateCoreWebView2CompositionController(
                    state->window,
                    Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                        [weak_state, ui_path](
                            const HRESULT controller_error,
                            ICoreWebView2CompositionController* composition_controller) -> HRESULT {
                            const auto state = weak_state.lock();
                            if (!state || state->shutting_down) {
                                return S_OK;
                            }
                            try {
                                if (FAILED(controller_error) || composition_controller == nullptr) {
                                    state->report(
                                        L"WebView2 composition controller creation failed.");
                                    return S_OK;
                                }
                                state->composition_controller = composition_controller;
                                THROW_IF_FAILED(composition_controller->QueryInterface(
                                    IID_PPV_ARGS(state->controller.put())));
                                THROW_IF_FAILED(
                                    state->controller->get_CoreWebView2(state->webview.put()));
                                THROW_IF_FAILED(composition_controller->put_RootVisualTarget(
                                    state->root_visual.get()));
                                // RootVisualTarget changes are part of our DirectComposition
                                // visual tree and are not presented until this device commits.
                                THROW_IF_FAILED(state->composition_device->Commit());
                                RECT bounds{0, 0, 440, 560};
                                THROW_IF_FAILED(state->controller->put_Bounds(bounds));
                                // A composition WebView may defer its first document while the
                                // controller itself is invisible. The HWND remains hidden until
                                // tick() validates the selected foreground target.
                                THROW_IF_FAILED(state->controller->put_IsVisible(TRUE));

                                wowai::platform::ComPtr<ICoreWebView2Controller2> controller2;
                                if (SUCCEEDED(state->controller->QueryInterface(
                                        IID_PPV_ARGS(controller2.put())))) {
                                    const COREWEBVIEW2_COLOR transparent{0, 0, 0, 0};
                                    THROW_IF_FAILED(
                                        controller2->put_DefaultBackgroundColor(transparent));
                                }

                                wowai::platform::ComPtr<ICoreWebView2Settings> settings;
                                THROW_IF_FAILED(state->webview->get_Settings(settings.put()));
                                THROW_IF_FAILED(settings->put_IsScriptEnabled(TRUE));
                                THROW_IF_FAILED(settings->put_IsWebMessageEnabled(TRUE));
                                THROW_IF_FAILED(
                                    settings->put_AreDefaultScriptDialogsEnabled(FALSE));
                                THROW_IF_FAILED(settings->put_IsStatusBarEnabled(FALSE));
                                THROW_IF_FAILED(settings->put_AreDevToolsEnabled(FALSE));
                                THROW_IF_FAILED(settings->put_AreDefaultContextMenusEnabled(FALSE));
                                THROW_IF_FAILED(settings->put_AreHostObjectsAllowed(FALSE));
                                THROW_IF_FAILED(settings->put_IsZoomControlEnabled(FALSE));
                                THROW_IF_FAILED(settings->put_IsBuiltInErrorPageEnabled(FALSE));

                                wowai::platform::ComPtr<ICoreWebView2_3> webview3;
                                THROW_IF_FAILED(
                                    state->webview->QueryInterface(IID_PPV_ARGS(webview3.put())));
                                THROW_IF_FAILED(webview3->SetVirtualHostNameToFolderMapping(
                                    overlay_host, ui_path.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY));
                                state->document_source = utf8_from_wide(overlay_uri);

                                EventRegistrationToken navigation_token{};
                                THROW_IF_FAILED(state->webview->add_NavigationStarting(
                                    Callback<ICoreWebView2NavigationStartingEventHandler>(
                                        [weak_state](ICoreWebView2*,
                                                     ICoreWebView2NavigationStartingEventArgs* args)
                                            -> HRESULT {
                                            wil::unique_cotaskmem_string uri;
                                            const auto state = weak_state.lock();
                                            if (!state || state->shutting_down) {
                                                return S_OK;
                                            }
                                            const bool has_uri =
                                                SUCCEEDED(args->get_Uri(&uri)) && uri != nullptr;
                                            const std::wstring_view uri_view =
                                                has_uri ? std::wstring_view{uri.get()}
                                                        : std::wstring_view{};
                                            if (uri_view == overlay_uri) {
                                                return S_OK;
                                            }
                                            if (has_uri) {
                                                args->put_Cancel(TRUE);
                                            }
                                            const std::wstring blocked =
                                                has_uri ? std::wstring{uri_view} : L"<unavailable>";
                                            state->report(
                                                L"Blocked unexpected overlay navigation: " +
                                                blocked.substr(0, 160));
                                            return S_OK;
                                        })
                                        .Get(),
                                    &navigation_token));

                                EventRegistrationToken navigation_completed_token{};
                                THROW_IF_FAILED(state->webview->add_NavigationCompleted(
                                    Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                        [weak_state](ICoreWebView2*,
                                                     ICoreWebView2NavigationCompletedEventArgs*
                                                         args) -> HRESULT {
                                            const auto state = weak_state.lock();
                                            if (!state || state->shutting_down) {
                                                return S_OK;
                                            }
                                            BOOL succeeded = FALSE;
                                            COREWEBVIEW2_WEB_ERROR_STATUS status{};
                                            args->get_IsSuccess(&succeeded);
                                            args->get_WebErrorStatus(&status);
                                            state->report(
                                                succeeded != FALSE
                                                    ? L"Overlay document navigation completed; "
                                                      L"waiting for the ready message."
                                                    : L"Overlay document navigation failed with "
                                                      L"WebView2 status " +
                                                          std::to_wstring(
                                                              static_cast<int>(status)) +
                                                          L".");
                                            return S_OK;
                                        })
                                        .Get(),
                                    &navigation_completed_token));

                                EventRegistrationToken message_token{};
                                THROW_IF_FAILED(state->webview->add_WebMessageReceived(
                                    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [weak_state](ICoreWebView2*,
                                                     ICoreWebView2WebMessageReceivedEventArgs* args)
                                            -> HRESULT {
                                            const auto state = weak_state.lock();
                                            if (!state || state->shutting_down) {
                                                return S_OK;
                                            }
                                            try {
                                                wil::unique_cotaskmem_string source;
                                                wil::unique_cotaskmem_string message;
                                                if (FAILED(args->get_Source(&source)) ||
                                                    FAILED(
                                                        args->TryGetWebMessageAsString(&message)) ||
                                                    source == nullptr || message == nullptr) {
                                                    return S_OK;
                                                }
                                                state->report(
                                                    L"Overlay document message received.");
                                                const BridgeResult parsed =
                                                    parse_web_message(utf8_from_wide(source.get()),
                                                                      utf8_from_wide(message.get()),
                                                                      state->document_source);
                                                if (!parsed.message) {
                                                    state->post_json(make_status_message(
                                                        "Blocked invalid UI message: " +
                                                            parsed.error,
                                                        true));
                                                    return S_OK;
                                                }
                                                switch (parsed.message->kind) {
                                                case WebMessageKind::ready:
                                                    state->document_ready = true;
                                                    state->post_json(make_status_message(
                                                        "覆盖层就绪；插件不是必需项。"));
                                                    state->report(
                                                        L"Transparent WebView2 overlay document "
                                                        L"is ready. Activate the selected WoW "
                                                        L"window to show it.");
                                                    break;
                                                case WebMessageKind::send_message:
                                                    if (!state->message_sink) {
                                                        state->post_json(make_status_message(
                                                            "本地模型尚未配置。请配置 Host、Ollama "
                                                            "端点和模型后重试。",
                                                            true));
                                                        break;
                                                    }
                                                    state->post_json(make_status_message(
                                                        "正在通过 Codex 请求本地模型…"));
                                                    state->message_sink(
                                                        std::move(parsed.message->text));
                                                    break;
                                                case WebMessageKind::set_interaction:
                                                    ::PostMessageW(state->window, WM_APP + 20,
                                                                   parsed.message->enabled ? 1 : 0,
                                                                   0);
                                                    break;
                                                case WebMessageKind::open_external:
                                                    ::ShellExecuteW(
                                                        state->window, L"open",
                                                        std::wstring(parsed.message->text.begin(),
                                                                     parsed.message->text.end())
                                                            .c_str(),
                                                        nullptr, nullptr, SW_SHOWNORMAL);
                                                    break;
                                                }
                                                return S_OK;
                                            } catch (...) {
                                                state->post_json(make_status_message(
                                                    "Blocked malformed UI message.", true));
                                                return S_OK;
                                            }
                                        })
                                        .Get(),
                                    &message_token));

                                THROW_IF_FAILED(state->webview->Navigate(overlay_uri));
                                state->webview_initialized = true;
                                state->report(L"WebView2 composition controller initialized; "
                                              L"waiting for the overlay document.");
                                return S_OK;
                            } catch (...) {
                                state->report(L"WebView2 security configuration failed; the "
                                              L"overlay remains hidden.");
                                return S_OK;
                            }
                        })
                        .Get());
            })
            .Get());
    if (FAILED(create_result)) {
        state_->report(
            L"WebView2 Runtime startup failed. Repair or install the Evergreen Runtime.");
    }
}

OverlayWindow::~OverlayWindow() {
    if (state_) {
        state_->shutting_down = true;
        if (state_->controller) {
            state_->controller->Close();
        }
        if (state_->window != nullptr) {
            MSG pending{};
            while (::PeekMessageW(&pending, state_->window, WM_APP + 21, WM_APP + 21,
                                  PM_REMOVE) != FALSE) {
                delete reinterpret_cast<std::string*>(pending.lParam);
            }
            ::DestroyWindow(state_->window);
            state_->window = nullptr;
        }
    }
    state_.reset();
    window_class_.reset();
}

void OverlayWindow::set_target(const HWND target,
                               const std::optional<wowai::capture::Rect> content_rect) noexcept {
    state_->target = target;
    state_->content_rect = content_rect;
    tick();
}

void OverlayWindow::clear_target() noexcept {
    state_->target = nullptr;
    state_->content_rect.reset();
    if (state_->controller) {
        state_->controller->put_IsVisible(FALSE);
    }
    ::ShowWindow(state_->window, SW_HIDE);
}

void OverlayWindow::tick() noexcept {
    if (!state_ || state_->window == nullptr) {
        return;
    }
    RECT client{};
    POINT origin{};
    const bool target_valid = state_->target != nullptr && ::IsWindow(state_->target) != FALSE;
    const bool client_usable = target_valid && ::GetClientRect(state_->target, &client) != FALSE &&
                               client.right > client.left && client.bottom > client.top &&
                               ::ClientToScreen(state_->target, &origin) != FALSE;
    const HWND foreground = ::GetForegroundWindow();
    const VisibilityContext visibility{target_valid,
                                       target_valid && ::IsIconic(state_->target) != FALSE,
                                       foreground == state_->target,
                                       foreground == state_->window,
                                       state_->interaction_enabled,
                                       client_usable};
    if (!state_->webview_initialized) {
        if (state_->controller) {
            state_->controller->put_IsVisible(FALSE);
        }
        ::ShowWindow(state_->window, SW_HIDE);
        return;
    }
    if (!state_->document_ready) {
        // Keep the composition controller active so the first local document can
        // load. Some WebView2 Runtime builds defer an invisible composition host,
        // so present a 1x1 host outside the virtual desktop until it is ready.
        ::SetWindowPos(state_->window, HWND_TOPMOST, -32000, -32000, 1, 1,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return;
    }
    if (!should_show(visibility)) {
        if (state_->controller) {
            state_->controller->put_IsVisible(FALSE);
        }
        ::ShowWindow(state_->window, SW_HIDE);
        return;
    }

    wowai::capture::Rect placement_source{origin.x, origin.y, origin.x + client.right,
                                          origin.y + client.bottom};
    if (state_->content_rect && state_->content_rect->valid() &&
        state_->content_rect->left >= client.left && state_->content_rect->top >= client.top &&
        state_->content_rect->right <= client.right &&
        state_->content_rect->bottom <= client.bottom) {
        placement_source = {
            origin.x + state_->content_rect->left, origin.y + state_->content_rect->top,
            origin.x + state_->content_rect->right, origin.y + state_->content_rect->bottom};
    }
    const wowai::capture::Rect placement = place_relative_to_client(placement_source);
    ::SetWindowPos(state_->window, HWND_TOPMOST, placement.left, placement.top, placement.width(),
                   placement.height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (state_->controller) {
        RECT bounds{0, 0, placement.width(), placement.height()};
        state_->controller->put_Bounds(bounds);
        state_->controller->NotifyParentWindowPositionChanged();
        state_->controller->put_IsVisible(TRUE);
    }
}

void OverlayWindow::set_interaction_enabled(const bool enabled) noexcept {
    if (!state_ || state_->interaction_enabled == enabled) {
        return;
    }
    state_->interaction_enabled = enabled;
    apply_interaction_style(state_->window, enabled);
    if (!enabled && state_->target != nullptr && ::IsWindow(state_->target) != FALSE) {
        ::SetForegroundWindow(state_->target);
    }
    state_->post_json(
        make_status_message(enabled ? "交互模式已启用。" : "鼠标穿透已启用；从托盘菜单恢复交互。"));
    tick();
}

void OverlayWindow::toggle_interaction() noexcept {
    set_interaction_enabled(!interaction_enabled());
}

void OverlayWindow::post_assistant_message(std::string text) noexcept {
    if (!state_ || state_->window == nullptr) {
        return;
    }
    auto json = std::unique_ptr<std::string>{
        new (std::nothrow) std::string{make_assistant_message(text)}};
    if (!json || ::PostMessageW(state_->window, WM_APP + 21, 0,
                                reinterpret_cast<LPARAM>(json.get())) == FALSE) {
        return;
    }
    static_cast<void>(json.release());
}

void OverlayWindow::post_status(std::string text, const bool error) noexcept {
    if (!state_ || state_->window == nullptr) {
        return;
    }
    auto json = std::unique_ptr<std::string>{
        new (std::nothrow) std::string{make_status_message(text, error)}};
    if (!json || ::PostMessageW(state_->window, WM_APP + 21, 0,
                                reinterpret_cast<LPARAM>(json.get())) == FALSE) {
        return;
    }
    static_cast<void>(json.release());
}

bool OverlayWindow::interaction_enabled() const noexcept {
    return state_ && state_->interaction_enabled;
}

bool OverlayWindow::initialized() const noexcept {
    return state_ && state_->webview_initialized && state_->document_ready;
}

LRESULT CALLBACK OverlayWindow::window_procedure(const HWND window, const UINT message,
                                                 const WPARAM wparam,
                                                 const LPARAM lparam) noexcept {
    State* raw_state = reinterpret_cast<State*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        raw_state = static_cast<State*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(raw_state));
    }
    if (raw_state == nullptr) {
        return ::DefWindowProcW(window, message, wparam, lparam);
    }
    // State lifetime is owned by OverlayWindow and always exceeds the HWND lifetime.
    return handle_message(raw_state, window, message, wparam, lparam);
}

LRESULT OverlayWindow::handle_message(State* const state, const HWND window, const UINT message,
                                      const WPARAM wparam, const LPARAM lparam) noexcept {
    if (message == WM_NCHITTEST && !state->interaction_enabled) {
        // WS_EX_TRANSPARENT changes paint ordering, but it does not by itself
        // guarantee cross-process mouse hit-test transparency. Explicitly reject
        // the hit so Windows can continue to the WoW window underneath.
        return HTTRANSPARENT;
    }
    if (message == WM_APP + 20) {
        state->interaction_enabled = wparam != 0;
        apply_interaction_style(window, state->interaction_enabled);
        if (!state->interaction_enabled && state->target != nullptr) {
            ::SetForegroundWindow(state->target);
        }
        return 0;
    }
    if (message == WM_APP + 21) {
        const std::unique_ptr<std::string> json{reinterpret_cast<std::string*>(lparam)};
        if (json) {
            state->post_json(*json);
        }
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_SETCURSOR && state->interaction_enabled && state->composition_controller) {
        HCURSOR cursor{};
        if (SUCCEEDED(state->composition_controller->get_Cursor(&cursor)) && cursor != nullptr) {
            ::SetCursor(cursor);
            return TRUE;
        }
    }
    if (!state->composition_controller || !state->interaction_enabled) {
        return ::DefWindowProcW(window, message, wparam, lparam);
    }

    COREWEBVIEW2_MOUSE_EVENT_KIND kind{};
    bool mouse_message = true;
    UINT32 mouse_data = 0;
    switch (message) {
    case WM_MOUSEMOVE:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE;
        if (!state->tracking_mouse) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            ::TrackMouseEvent(&tracking);
            state->tracking_mouse = true;
        }
        break;
    case WM_MOUSELEAVE:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE;
        state->tracking_mouse = false;
        break;
    case WM_LBUTTONDOWN:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN;
        ::SetForegroundWindow(window);
        ::SetFocus(window);
        state->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        ::SetCapture(window);
        break;
    case WM_LBUTTONUP:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;
        ::ReleaseCapture();
        break;
    case WM_LBUTTONDBLCLK:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOUBLE_CLICK;
        break;
    case WM_RBUTTONDOWN:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN;
        break;
    case WM_RBUTTONUP:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP;
        break;
    case WM_MOUSEWHEEL:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL;
        mouse_data = static_cast<UINT32>(GET_WHEEL_DELTA_WPARAM(wparam));
        break;
    case WM_MOUSEHWHEEL:
        kind = COREWEBVIEW2_MOUSE_EVENT_KIND_HORIZONTAL_WHEEL;
        mouse_data = static_cast<UINT32>(GET_WHEEL_DELTA_WPARAM(wparam));
        break;
    default:
        mouse_message = false;
        break;
    }
    if (mouse_message) {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
            ::ScreenToClient(window, &point);
        }
        state->composition_controller->SendMouseInput(kind, mouse_keys(wparam), mouse_data, point);
        return 0;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

} // namespace wowai::overlay
