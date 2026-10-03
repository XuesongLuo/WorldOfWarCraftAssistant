#include "wowai/platform/global_hotkey.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

class FakeRegistrar final : public wowai::platform::HotkeyRegistrar {
  public:
    bool allow{true};
    int registrations{};
    int unregistrations{};

    bool register_hotkey(HWND, int, std::uint32_t, std::uint32_t) noexcept override {
        ++registrations;
        return allow;
    }
    void unregister_hotkey(HWND, int) noexcept override { ++unregistrations; }
};

} // namespace

TEST_CASE("global hotkey can be registered, replaced, disabled, and released") {
    FakeRegistrar registrar;
    wowai::platform::GlobalHotkey hotkey{reinterpret_cast<HWND>(1), registrar};
    auto settings = wowai::storage::AssistantSettings::defaults();

    CHECK(hotkey.apply(settings) == wowai::platform::HotkeyApplyResult::registered);
    CHECK(hotkey.registered());
    CHECK(registrar.registrations == 1);

    settings.hotkey_virtual_key = 'G';
    CHECK(hotkey.apply(settings) == wowai::platform::HotkeyApplyResult::registered);
    CHECK(registrar.unregistrations == 1);

    settings.hotkey_enabled = false;
    CHECK(hotkey.apply(settings) == wowai::platform::HotkeyApplyResult::disabled);
    CHECK_FALSE(hotkey.registered());
    CHECK(registrar.unregistrations == 2);
}

TEST_CASE("global hotkey reports conflicts without claiming registration") {
    FakeRegistrar registrar;
    registrar.allow = false;
    wowai::platform::GlobalHotkey hotkey{reinterpret_cast<HWND>(1), registrar};
    CHECK(hotkey.apply(wowai::storage::AssistantSettings::defaults()) ==
          wowai::platform::HotkeyApplyResult::conflict);
    CHECK_FALSE(hotkey.registered());
}

TEST_CASE("assistant settings reject unmodified common game keys") {
    auto settings = wowai::storage::AssistantSettings::defaults();
    settings.hotkey_modifiers = MOD_NOREPEAT;
    settings.hotkey_virtual_key = 'W';
    CHECK_FALSE(settings.valid());
}

TEST_CASE("Windows reports a real global hotkey conflict without replacing it") {
    wowai::platform::WindowsHotkeyRegistrar registrar;
    bool tested{};
    for (std::uint32_t key = VK_F13; key <= VK_F24 && !tested; ++key) {
        if (!registrar.register_hotkey(nullptr, 0x7100, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT,
                                       key)) {
            continue;
        }
        tested = true;
        CHECK_FALSE(registrar.register_hotkey(nullptr, 0x7101,
                                              MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, key));
        registrar.unregister_hotkey(nullptr, 0x7101);
        registrar.unregister_hotkey(nullptr, 0x7100);
    }
    CHECK(tested);
}
