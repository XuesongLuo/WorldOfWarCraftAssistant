#include "wowai/platform/single_instance.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include <windows.h>

namespace {

std::wstring unique_mutex_name() {
    return L"Local\\WowAITest." + std::to_wstring(::GetCurrentProcessId()) + L"." +
           std::to_wstring(::GetTickCount64());
}

} // namespace

TEST_CASE("only the first owner of a named mutex is primary") {
    const std::wstring name = unique_mutex_name();

    wowai::platform::SingleInstance first{name};
    wowai::platform::SingleInstance second{name};

    CHECK(first.is_primary());
    CHECK_FALSE(second.is_primary());
}

TEST_CASE("single instance rejects an invalid name during construction") {
    CHECK_THROWS_AS(wowai::platform::SingleInstance{L""}, std::invalid_argument);
}
