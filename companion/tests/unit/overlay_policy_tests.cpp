#include "wowai/overlay/overlay_policy.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("overlay uses a client-relative bottom-right layout") {
    const wowai::capture::Rect client{-1920, 100, 0, 1180};
    const auto placed = wowai::overlay::place_relative_to_client(client);
    REQUIRE(placed == wowai::capture::Rect{-464, 596, -24, 1156});
}

TEST_CASE("overlay layout is clamped inside small clients") {
    const auto placed = wowai::overlay::place_relative_to_client({10, 20, 310, 220});
    REQUIRE(placed == wowai::capture::Rect{10, 20, 310, 220});
    REQUIRE_FALSE(wowai::overlay::place_relative_to_client({}).valid());
}

TEST_CASE("foreground gate permits only WoW or its interactive overlay") {
    wowai::overlay::VisibilityContext context{true, false, true, false, false, true};
    REQUIRE(wowai::overlay::should_show(context));

    context.selected_window_is_foreground = false;
    REQUIRE_FALSE(wowai::overlay::should_show(context));

    context.overlay_is_foreground = true;
    context.interaction_enabled = true;
    REQUIRE(wowai::overlay::should_show(context));

    context.selected_window_minimized = true;
    REQUIRE_FALSE(wowai::overlay::should_show(context));
}
