#include "wowai/capture/scene_awareness.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

TEST_CASE("scene awareness is explicit rate limited and does not auto resume") {
    using namespace std::chrono_literals;
    wowai::capture::SceneAwarenessSession session;
    const auto start = std::chrono::steady_clock::time_point{10s};
    const wowai::capture::ObservationGate allowed{true, true, false, true};

    CHECK_FALSE(session.active());
    session.start(wowai::capture::ObservationMode::scene, start);
    CHECK(session.evaluate(allowed, start).capture);
    CHECK_FALSE(session.evaluate(allowed, start + 900ms).capture);
    CHECK(session.evaluate(allowed, start + 1s).capture);

    const auto stopped = session.evaluate({true, false, false, true}, start + 1100ms);
    CHECK_FALSE(stopped.active);
    CHECK(stopped.stop_reason == wowai::capture::ObservationStopReason::not_foreground);
    CHECK_FALSE(session.evaluate(allowed, start + 3s).active);
}

TEST_CASE("coaching observes only with a visible indicator and stops on every window gate") {
    using namespace std::chrono_literals;
    const auto start = std::chrono::steady_clock::time_point{20s};
    wowai::capture::SceneAwarenessSession session;
    session.start(wowai::capture::ObservationMode::coaching, start);
    CHECK(session.evaluate({true, true, false, true}, start).capture);
    CHECK_FALSE(session.evaluate({true, true, false, true}, start + 400ms).capture);
    CHECK(session.evaluate({true, true, false, true}, start + 500ms).capture);

    session.start(wowai::capture::ObservationMode::coaching, start + 1s);
    CHECK(session.evaluate({true, true, false, false}, start + 1s).stop_reason ==
          wowai::capture::ObservationStopReason::indicator_hidden);
    session.start(wowai::capture::ObservationMode::coaching, start + 2s);
    CHECK(session.evaluate({true, true, true, true}, start + 2s).stop_reason ==
          wowai::capture::ObservationStopReason::minimized);
    session.start(wowai::capture::ObservationMode::coaching, start + 3s);
    CHECK(session.evaluate({false, false, false, true}, start + 3s).stop_reason ==
          wowai::capture::ObservationStopReason::window_closed);
}
