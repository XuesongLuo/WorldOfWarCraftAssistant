#include "wowai/capture/window_discovery.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {

wowai::capture::WindowCandidate candidate(const std::uint64_t fingerprint) {
    return {nullptr,
            1,
            {L"wow.exe", L"GxWindowClass", fingerprint},
            L"unstable title",
            {0, 0, 1920, 1080}};
}

} // namespace

TEST_CASE("WoW discovery identifies supported executables without using titles") {
    CHECK(wowai::capture::is_supported_wow_executable(L"Wow.exe"));
    CHECK(wowai::capture::is_supported_wow_executable(L"WOWCLASSIC.EXE"));
    CHECK_FALSE(wowai::capture::is_supported_wow_executable(L"World of Warcraft.exe"));
    CHECK_FALSE(wowai::capture::is_supported_wow_executable(L"notepad.exe"));
}

TEST_CASE("selection never guesses between multiple unmatched clients") {
    const std::vector candidates{candidate(10), candidate(20)};
    const auto result = wowai::capture::choose_window(candidates, std::nullopt);

    CHECK(result.kind == wowai::capture::SelectionKind::requires_user);
    CHECK_FALSE(result.candidate_index);
}

TEST_CASE("a saved stable identity selects exactly one matching client") {
    const std::vector candidates{candidate(10), candidate(20)};
    const auto result = wowai::capture::choose_window(candidates, candidates[1].identity);

    CHECK(result.kind == wowai::capture::SelectionKind::selected);
    REQUIRE(result.candidate_index);
    CHECK(*result.candidate_index == 1);
}

TEST_CASE("an ambiguous saved identity still requires manual selection") {
    const std::vector candidates{candidate(10), candidate(10)};
    const auto result = wowai::capture::choose_window(candidates, candidates[0].identity);

    CHECK(result.kind == wowai::capture::SelectionKind::requires_user);
}
