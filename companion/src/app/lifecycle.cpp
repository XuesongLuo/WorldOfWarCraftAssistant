#include "wowai/app/lifecycle.hpp"

#include <stdexcept>

namespace wowai::app {

std::string_view to_string(const LifecycleState state) noexcept {
    switch (state) {
    case LifecycleState::starting:
        return "Starting";
    case LifecycleState::waiting_for_wow:
        return "WaitingForWow";
    case LifecycleState::waiting_for_addon_panel:
        return "WaitingForAddonPanel";
    case LifecycleState::ready:
        return "Ready";
    case LifecycleState::capturing:
        return "Capturing";
    case LifecycleState::requesting:
        return "Requesting";
    case LifecycleState::displaying:
        return "Displaying";
    case LifecycleState::recoverable_error:
        return "RecoverableError";
    case LifecycleState::fatal_error:
        return "FatalError";
    }
    return "Unknown";
}

bool can_transition(const LifecycleState from, const LifecycleState to) noexcept {
    if (from == to) {
        return true;
    }
    if (from == LifecycleState::fatal_error) {
        return false;
    }
    if (to == LifecycleState::fatal_error) {
        return true;
    }
    if (to == LifecycleState::waiting_for_wow && from != LifecycleState::starting) {
        return true;
    }

    switch (from) {
    case LifecycleState::starting:
        return to == LifecycleState::waiting_for_wow;
    case LifecycleState::waiting_for_wow:
        return to == LifecycleState::waiting_for_addon_panel;
    case LifecycleState::waiting_for_addon_panel:
        return to == LifecycleState::ready || to == LifecycleState::recoverable_error;
    case LifecycleState::ready:
        return to == LifecycleState::capturing || to == LifecycleState::requesting ||
               to == LifecycleState::recoverable_error;
    case LifecycleState::capturing:
        return to == LifecycleState::requesting || to == LifecycleState::ready ||
               to == LifecycleState::recoverable_error;
    case LifecycleState::requesting:
        return to == LifecycleState::displaying || to == LifecycleState::recoverable_error;
    case LifecycleState::displaying:
        return to == LifecycleState::ready || to == LifecycleState::capturing ||
               to == LifecycleState::requesting;
    case LifecycleState::recoverable_error:
        return to == LifecycleState::waiting_for_addon_panel || to == LifecycleState::ready;
    case LifecycleState::fatal_error:
        return false;
    }
    return false;
}

LifecycleState Lifecycle::state() const noexcept { return state_; }

void Lifecycle::transition_to(const LifecycleState next) {
    if (!can_transition(state_, next)) {
        throw std::logic_error("invalid application lifecycle transition");
    }
    state_ = next;
}

void Lifecycle::on_wow_exited() {
    if (state_ != LifecycleState::fatal_error) {
        state_ = LifecycleState::waiting_for_wow;
    }
}

} // namespace wowai::app
