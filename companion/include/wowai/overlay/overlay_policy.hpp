#pragma once

#include "wowai/capture/geometry.hpp"

#include <cstdint>

namespace wowai::overlay {

struct RelativeLayout {
    std::int32_t width{440};
    std::int32_t height{560};
    std::int32_t right_margin{24};
    std::int32_t bottom_margin{24};
};

struct VisibilityContext {
    bool selected_window_valid{};
    bool selected_window_minimized{};
    bool selected_window_is_foreground{};
    bool overlay_is_foreground{};
    bool interaction_enabled{};
    bool client_rect_usable{};
};

[[nodiscard]] wowai::capture::Rect
place_relative_to_client(const wowai::capture::Rect& client_rect,
                         const RelativeLayout& layout = {}) noexcept;

[[nodiscard]] bool should_show(const VisibilityContext& context) noexcept;

} // namespace wowai::overlay
