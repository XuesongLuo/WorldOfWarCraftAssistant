#include "wowai/capture/calibration.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("manual calibration scales with the selected client") {
    wowai::capture::ManualCalibration calibration;
    REQUIRE(calibration.set({100, 50, 900, 650}, {1000, 700}));

    const auto resolved = calibration.resolve({2000, 1400});
    REQUIRE(resolved);
    CHECK(*resolved == wowai::capture::Rect{200, 100, 1800, 1300});
}

TEST_CASE("manual calibration rejects unsafe rectangles and can reset") {
    wowai::capture::ManualCalibration calibration;
    CHECK_FALSE(calibration.set({-1, 0, 100, 100}, {1920, 1080}));
    CHECK_FALSE(calibration.set({0, 0, 1921, 1080}, {1920, 1080}));
    REQUIRE(calibration.set({10, 20, 300, 400}, {1920, 1080}));
    calibration.reset();
    CHECK_FALSE(calibration.configured());
    CHECK_FALSE(calibration.resolve({1920, 1080}));
}
