#pragma once

#include "wowai/capture/geometry.hpp"
#include "wowai/overlay/web_message_bridge.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <windows.h>

namespace wowai::overlay {

inline constexpr wchar_t overlay_window_class[] = L"WorldOfWarcraftAssistant.Overlay.Window.v1";

class OverlayWindow final {
  public:
    using StatusSink = std::function<void(std::wstring)>;
    using MessageSink = std::function<void(WebMessage)>;

    OverlayWindow(HINSTANCE instance, StatusSink status_sink, MessageSink message_sink = {});
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    void set_target(HWND target,
                    std::optional<wowai::capture::Rect> content_rect = std::nullopt) noexcept;
    void clear_target() noexcept;
    void tick() noexcept;
    void set_interaction_enabled(bool enabled) noexcept;
    void toggle_interaction() noexcept;
    void toggle_visibility() noexcept;
    void set_user_visible(bool visible) noexcept;
    void apply_appearance(std::uint32_t opacity_percent, std::uint32_t font_size_px) noexcept;
    void post_assistant_message(std::string text) noexcept;
    void post_status(std::string text, bool error = false) noexcept;
    void post_request_state(std::string phase, std::string text, std::string error_code = {},
                            bool retryable = false, std::string action = {}) noexcept;
    void post_screenshot_preview(std::string png_base64, std::int32_t width, std::int32_t height,
                                 bool privacy_mask_applied) noexcept;
    void clear_screenshot_preview() noexcept;
    void post_observation(std::string mode, std::string scene, std::string captured_at,
                          double confidence, std::string reason) noexcept;

    [[nodiscard]] bool interaction_enabled() const noexcept;
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] bool user_visible() const noexcept;
    [[nodiscard]] bool owns_foreground() const noexcept;

  private:
    struct State;
    class WindowClassRegistration;

    static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam,
                                             LPARAM lparam) noexcept;
    static LRESULT handle_message(State* state, HWND window, UINT message, WPARAM wparam,
                                  LPARAM lparam) noexcept;

    std::unique_ptr<WindowClassRegistration> window_class_;
    std::shared_ptr<State> state_;
};

} // namespace wowai::overlay
