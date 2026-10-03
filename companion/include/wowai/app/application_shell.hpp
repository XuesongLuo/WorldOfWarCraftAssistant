#pragma once

#include "wowai/app/lifecycle.hpp"
#include "wowai/app/settings_window.hpp"
#include "wowai/capture/anchor_detector.hpp"
#include "wowai/capture/calibration.hpp"
#include "wowai/capture/image_encoding.hpp"
#include "wowai/capture/image_processing.hpp"
#include "wowai/capture/scene_awareness.hpp"
#include "wowai/capture/selection_store.hpp"
#include "wowai/capture/visual_bridge.hpp"
#include "wowai/capture/window_capture.hpp"
#include "wowai/capture/window_discovery.hpp"
#include "wowai/codex/assistant_session.hpp"
#include "wowai/overlay/overlay_window.hpp"
#include "wowai/platform/global_hotkey.hpp"
#include "wowai/platform/resources.hpp"
#include "wowai/storage/application_paths.hpp"
#include "wowai/storage/credential_store.hpp"
#include "wowai/storage/local_data.hpp"
#include "wowai/storage/local_database.hpp"
#include "wowai/storage/safe_log.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
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
    void show_settings() noexcept;
    [[nodiscard]] bool apply_settings(const wowai::storage::AssistantSettings& settings,
                                      const std::optional<std::string>& api_key) noexcept;
    [[nodiscard]] bool delete_cloud_credential(std::string_view provider,
                                               std::string_view profile) noexcept;
    void reload_assistant_session() noexcept;
    [[nodiscard]] bool delete_local_data() noexcept;
    void show_tray_menu() noexcept;
    void refresh_wow_windows() noexcept;
    void select_candidate(std::size_t index, bool remember) noexcept;
    void detect_anchor() noexcept;
    void begin_manual_calibration() noexcept;
    void reset_calibration() noexcept;
    void set_status(std::wstring detail) noexcept;
    void submit_question(std::string question) noexcept;
    void cancel_request() noexcept;
    void handle_overlay_message(wowai::overlay::WebMessage message) noexcept;
    void capture_screenshot(bool mask_chat, bool selected_region) noexcept;
    void confirm_screenshot() noexcept;
    void discard_screenshot() noexcept;
    void start_observation(wowai::capture::ObservationMode mode) noexcept;
    void pause_observation() noexcept;
    void tick_observation() noexcept;
    void process_observation_frame() noexcept;

    struct PendingScreenshot {
        wowai::capture::EncodedImage encoded;
        std::int32_t width{};
        std::int32_t height{};
        std::string captured_at;
        bool privacy_mask_applied{};
        bool confirmed{};
        std::string capture_scope;
        std::string upload_confirmed_at;
    };

    HINSTANCE instance_{};
    std::unique_ptr<WindowClassRegistration> window_class_;
    wowai::platform::UniqueWindow window_;
    std::unique_ptr<TrayIcon> tray_icon_;
    wowai::storage::ApplicationPaths paths_;
    std::unique_ptr<wowai::storage::LocalDatabase> database_;
    std::unique_ptr<wowai::storage::CredentialStore> credential_store_;
    std::unique_ptr<wowai::storage::LocalDataCleaner> local_data_cleaner_;
    std::unique_ptr<wowai::storage::SafeLog> safe_log_;
    wowai::storage::AssistantSettings settings_;
    wowai::platform::WindowsHotkeyRegistrar hotkey_registrar_;
    std::unique_ptr<wowai::platform::GlobalHotkey> global_hotkey_;
    std::unique_ptr<SettingsWindow> settings_window_;
    std::unique_ptr<wowai::codex::AssistantSession> assistant_session_;
    std::unique_ptr<wowai::overlay::OverlayWindow> overlay_window_;
    std::jthread request_thread_;
    std::atomic_bool request_active_{false};
    std::uint32_t cloud_session_requests_{};
    std::string assistant_configuration_error_;
    std::string assistant_configuration_code_;
    Lifecycle lifecycle_;
    wowai::capture::WindowDiscovery window_discovery_;
    wowai::capture::WindowCapture window_capture_;
    wowai::capture::AnchorDetector anchor_detector_;
    wowai::capture::ManualCalibration manual_calibration_;
    std::unique_ptr<wowai::capture::SelectionStore> selection_store_;
    std::vector<wowai::capture::WindowCandidate> candidates_;
    std::optional<wowai::capture::WindowCandidate> selected_candidate_;
    std::optional<wowai::capture::Rect> content_rect_;
    std::optional<PendingScreenshot> pending_screenshot_;
    wowai::capture::SceneAwarenessSession observation_session_;
    wowai::capture::VisualBridgeGate visual_bridge_gate_;
    nlohmann::json observations_{nlohmann::json::array()};
    nlohmann::json visual_bridge_context_{nullptr};
    std::wstring status_detail_;
};

} // namespace wowai::app
