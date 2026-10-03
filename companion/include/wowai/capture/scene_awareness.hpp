#pragma once

#include <chrono>
#include <string_view>

namespace wowai::capture {

enum class ObservationMode { scene, coaching };
enum class ObservationStopReason { none, player_paused, not_foreground, minimized, window_closed, indicator_hidden };

struct ObservationGate {
    bool window_exists{};
    bool foreground{};
    bool minimized{};
    bool indicator_visible{};
};

struct ObservationDecision {
    bool capture{};
    bool active{};
    ObservationStopReason stop_reason{ObservationStopReason::none};
};

class SceneAwarenessSession final {
  public:
    void start(ObservationMode mode, std::chrono::steady_clock::time_point now) noexcept;
    void pause() noexcept;
    [[nodiscard]] ObservationDecision evaluate(const ObservationGate& gate,
                                               std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] ObservationMode mode() const noexcept { return mode_; }

  private:
    bool active_{};
    ObservationMode mode_{ObservationMode::scene};
    std::chrono::steady_clock::time_point last_capture_{};
};

[[nodiscard]] std::string_view to_string(ObservationStopReason reason) noexcept;

} // namespace wowai::capture
