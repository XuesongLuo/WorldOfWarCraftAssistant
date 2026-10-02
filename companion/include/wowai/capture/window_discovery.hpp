#pragma once

#include "wowai/capture/geometry.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

namespace wowai::capture {

struct WindowIdentity {
    std::wstring executable_name;
    std::wstring window_class;
    std::uint64_t installation_fingerprint{};

    bool operator==(const WindowIdentity&) const = default;
};

struct WindowCandidate {
    HWND window{};
    std::uint32_t process_id{};
    WindowIdentity identity;
    std::wstring title;
    Rect client_rect_screen;
};

enum class SelectionKind { none, selected, requires_user };

struct SelectionResult {
    SelectionKind kind{SelectionKind::none};
    std::optional<std::size_t> candidate_index;
};

[[nodiscard]] bool is_supported_wow_executable(std::wstring_view executable_name) noexcept;
[[nodiscard]] SelectionResult
choose_window(const std::vector<WindowCandidate>& candidates,
              const std::optional<WindowIdentity>& preferred) noexcept;

class WindowDiscovery final {
  public:
    [[nodiscard]] std::vector<WindowCandidate> discover() const;
};

} // namespace wowai::capture
