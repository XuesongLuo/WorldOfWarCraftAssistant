#include "wowai/app/lifecycle.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

TEST_CASE("lifecycle follows the normal request path") {
    wowai::app::Lifecycle lifecycle;
    REQUIRE(lifecycle.state() == wowai::app::LifecycleState::starting);

    lifecycle.transition_to(wowai::app::LifecycleState::waiting_for_wow);
    lifecycle.transition_to(wowai::app::LifecycleState::ready);
    lifecycle.transition_to(wowai::app::LifecycleState::capturing);
    lifecycle.transition_to(wowai::app::LifecycleState::requesting);
    lifecycle.transition_to(wowai::app::LifecycleState::displaying);
    lifecycle.transition_to(wowai::app::LifecycleState::ready);

    CHECK(lifecycle.state() == wowai::app::LifecycleState::ready);
}

TEST_CASE("lifecycle rejects an invalid transition without changing state") {
    wowai::app::Lifecycle lifecycle;

    CHECK_THROWS_AS(lifecycle.transition_to(wowai::app::LifecycleState::displaying),
                    std::logic_error);
    CHECK(lifecycle.state() == wowai::app::LifecycleState::starting);
}

TEST_CASE("WoW exit returns every non-fatal state to waiting") {
    wowai::app::Lifecycle lifecycle;
    lifecycle.transition_to(wowai::app::LifecycleState::waiting_for_wow);
    lifecycle.transition_to(wowai::app::LifecycleState::ready);
    lifecycle.transition_to(wowai::app::LifecycleState::requesting);

    lifecycle.on_wow_exited();

    CHECK(lifecycle.state() == wowai::app::LifecycleState::waiting_for_wow);
}

TEST_CASE("selecting WoW can become ready without an addon") {
    wowai::app::Lifecycle lifecycle;
    lifecycle.transition_to(wowai::app::LifecycleState::waiting_for_wow);

    CHECK_NOTHROW(lifecycle.transition_to(wowai::app::LifecycleState::ready));
    CHECK(lifecycle.state() == wowai::app::LifecycleState::ready);
}

TEST_CASE("fatal state is terminal") {
    wowai::app::Lifecycle lifecycle;
    lifecycle.transition_to(wowai::app::LifecycleState::fatal_error);

    lifecycle.on_wow_exited();
    CHECK(lifecycle.state() == wowai::app::LifecycleState::fatal_error);
    CHECK_THROWS_AS(lifecycle.transition_to(wowai::app::LifecycleState::waiting_for_wow),
                    std::logic_error);
}
