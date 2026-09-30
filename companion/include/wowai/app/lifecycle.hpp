#pragma once

#include <string_view>

namespace wowai::app {

enum class LifecycleState {
    starting,
    waiting_for_wow,
    waiting_for_addon_panel,
    ready,
    capturing,
    requesting,
    displaying,
    recoverable_error,
    fatal_error,
};

[[nodiscard]] std::string_view to_string(LifecycleState state) noexcept;
[[nodiscard]] bool can_transition(LifecycleState from, LifecycleState to) noexcept;

class Lifecycle final {
  public:
    [[nodiscard]] LifecycleState state() const noexcept;
    void transition_to(LifecycleState next);
    void on_wow_exited();

  private:
    LifecycleState state_{LifecycleState::starting};
};

} // namespace wowai::app
