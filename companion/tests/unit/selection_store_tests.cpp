#include "wowai/capture/selection_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <windows.h>

TEST_CASE("window selection store round trips a non-identifying stable identity") {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"wowai-selection-" + std::to_wstring(::GetCurrentProcessId()) + L".txt");
    const wowai::capture::SelectionStore store{path};
    store.clear();
    const wowai::capture::WindowIdentity expected{L"wow.exe", L"GxWindowClass", 0x1234abcd};

    store.save(expected);
    const auto actual = store.load();

    REQUIRE(actual);
    CHECK(*actual == expected);
    store.clear();
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("window selection store fails closed for malformed content") {
    const auto path =
        std::filesystem::temp_directory_path() /
        (L"wowai-selection-malformed-" + std::to_wstring(::GetCurrentProcessId()) + L".txt");
    {
        std::wofstream output(path);
        output << L"not-a-wow-process\nclass\ninvalid-number\n";
    }
    const wowai::capture::SelectionStore store{path};
    CHECK_FALSE(store.load());
    store.clear();
}
