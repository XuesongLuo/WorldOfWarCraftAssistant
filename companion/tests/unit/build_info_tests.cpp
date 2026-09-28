#include "wowai/app/build_info.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("build metadata is non-empty", "[app][smoke]") {
    REQUIRE_FALSE(wowai::app::application_name().empty());
    REQUIRE_FALSE(wowai::app::application_version().empty());
}

TEST_CASE("development version is explicit", "[app][boundary]") {
    REQUIRE(wowai::app::application_version().ends_with("-dev"));
}
