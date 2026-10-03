#include "wowai/capture/scene_awareness.hpp"

namespace wowai::capture {

void SceneAwarenessSession::start(const ObservationMode mode,
                                  const std::chrono::steady_clock::time_point now) noexcept {
    mode_ = mode;
    active_ = true;
    last_capture_ = now - std::chrono::seconds{2};
}

void SceneAwarenessSession::pause() noexcept {
    active_ = false;
}

ObservationDecision SceneAwarenessSession::evaluate(
    const ObservationGate& gate, const std::chrono::steady_clock::time_point now) noexcept {
    if (!active_) {
        return {false, false, ObservationStopReason::player_paused};
    }
    ObservationStopReason reason = ObservationStopReason::none;
    if (!gate.window_exists) {
        reason = ObservationStopReason::window_closed;
    } else if (gate.minimized) {
        reason = ObservationStopReason::minimized;
    } else if (!gate.foreground) {
        reason = ObservationStopReason::not_foreground;
    } else if (!gate.indicator_visible) {
        reason = ObservationStopReason::indicator_hidden;
    }
    if (reason != ObservationStopReason::none) {
        active_ = false;
        return {false, false, reason};
    }

    const auto interval = mode_ == ObservationMode::coaching ? std::chrono::milliseconds{500}
                                                              : std::chrono::seconds{1};
    if (now - last_capture_ < interval) {
        return {false, true, ObservationStopReason::none};
    }
    last_capture_ = now;
    return {true, true, ObservationStopReason::none};
}

std::string_view to_string(const ObservationStopReason reason) noexcept {
    switch (reason) {
    case ObservationStopReason::none:
        return "none";
    case ObservationStopReason::player_paused:
        return "player-paused";
    case ObservationStopReason::not_foreground:
        return "not-foreground";
    case ObservationStopReason::minimized:
        return "minimized";
    case ObservationStopReason::window_closed:
        return "window-closed";
    case ObservationStopReason::indicator_hidden:
        return "indicator-hidden";
    }
    return "unknown";
}

} // namespace wowai::capture
