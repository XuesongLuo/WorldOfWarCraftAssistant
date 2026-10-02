#pragma once

#include "wowai/app/lifecycle.hpp"
#include "wowai/capture/anchor_detector.hpp"
#include "wowai/capture/calibration.hpp"
#include "wowai/capture/selection_store.hpp"
#include "wowai/capture/window_capture.hpp"
#include "wowai/capture/window_discovery.hpp"
#include "wowai/overlay/overlay_window.hpp"
#include "wowai/platform/resources.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

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
    void refresh_wow_windows() noexcept;
    void select_candidate(std::size_t index, bool remember) noexcept;
    void detect_anchor() noexcept;
    void begin_manual_calibration() noexcept;
    void reset_calibration() noexcept;
    void set_status(std::wstring detail) noexcept;

    HINSTANCE instance_{};
    std::unique_ptr<WindowClassRegistration> window_class_;
    wowai::platform::UniqueWindow window_;
    std::unique_ptr<TrayIcon> tray_icon_;
    std::unique_ptr<wowai::overlay::OverlayWindow> overlay_window_;
    Lifecycle lifecycle_;
    wowai::capture::WindowDiscovery window_discovery_;
    wowai::capture::WindowCapture window_capture_;
    wowai::capture::AnchorDetector anchor_detector_;
    wowai::capture::ManualCalibration manual_calibration_;
    std::unique_ptr<wowai::capture::SelectionStore> selection_store_;
    std::vector<wowai::capture::WindowCandidate> candidates_;
    std::optional<wowai::capture::WindowCandidate> selected_candidate_;
    std::optional<wowai::capture::Rect> content_rect_;
    std::wstring status_detail_;
};

} // namespace wowai::app
