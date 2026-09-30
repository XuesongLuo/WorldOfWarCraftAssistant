#include "wowai/platform/resources.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include <windows.h>

TEST_CASE("owned Win32 handles are released while unwinding an exception") {
    DWORD handles_before = 0;
    REQUIRE(::GetProcessHandleCount(::GetCurrentProcess(), &handles_before) != FALSE);

    try {
        wowai::platform::UniqueHandle event{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        REQUIRE(event);
        throw std::runtime_error("exercise exceptional cleanup");
    } catch (const std::runtime_error&) {
    }

    DWORD handles_after = 0;
    REQUIRE(::GetProcessHandleCount(::GetCurrentProcess(), &handles_after) != FALSE);
    CHECK(handles_after == handles_before);
}

TEST_CASE("STA initialization owns and releases the COM apartment") {
    auto apartment = wowai::platform::initialize_sta();
    CHECK(apartment);
}
