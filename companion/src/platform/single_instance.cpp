#include "wowai/platform/single_instance.hpp"

#include <stdexcept>
#include <string>
#include <system_error>

namespace wowai::platform {

SingleInstance::SingleInstance(const std::wstring_view mutex_name) {
    if (mutex_name.empty() || mutex_name.find(L'\0') != std::wstring_view::npos) {
        throw std::invalid_argument("single-instance mutex name must be non-empty");
    }

    const std::wstring owned_name{mutex_name};
    HANDLE raw_mutex = ::CreateMutexW(nullptr, FALSE, owned_name.c_str());
    if (raw_mutex == nullptr) {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                "CreateMutexW failed");
    }

    const DWORD creation_error = ::GetLastError();
    mutex_.reset(raw_mutex);
    primary_ = creation_error != ERROR_ALREADY_EXISTS;
}

bool SingleInstance::is_primary() const noexcept { return primary_; }

bool SingleInstance::notify_existing_window(const std::wstring_view window_class,
                                            const UINT activation_message,
                                            const DWORD wait_milliseconds) noexcept {
    const std::wstring owned_class{window_class};
    constexpr DWORD retry_interval_milliseconds = 50;
    DWORD elapsed = 0;

    do {
        if (HWND window = ::FindWindowW(owned_class.c_str(), nullptr); window != nullptr) {
            DWORD_PTR ignored_result = 0;
            const bool notified =
                ::SendMessageTimeoutW(window, activation_message, 0, 0,
                                      SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &ignored_result) != 0;
            if (notified) {
                // The newly launched process is allowed to transfer foreground activation to the
                // existing top-level window. The message also restores a minimized window.
                ::SetForegroundWindow(window);
            }
            return notified;
        }
        if (elapsed >= wait_milliseconds) {
            break;
        }
        ::Sleep(retry_interval_milliseconds);
        elapsed += retry_interval_milliseconds;
    } while (true);

    return false;
}

} // namespace wowai::platform
