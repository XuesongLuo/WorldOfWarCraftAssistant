#include "wowai/overlay/overlay_policy.hpp"

#include <algorithm>

namespace wowai::overlay {

wowai::capture::Rect place_relative_to_client(const wowai::capture::Rect& client_rect,
                                              const RelativeLayout& layout) noexcept {
    if (!client_rect.valid()) {
        return {};
    }

    const std::int32_t available_width = client_rect.width();
    const std::int32_t available_height = client_rect.height();
    const std::int32_t width = std::clamp(layout.width, 1, available_width);
    const std::int32_t height = std::clamp(layout.height, 1, available_height);
    const std::int32_t right_margin = std::clamp(layout.right_margin, 0, available_width - width);
    const std::int32_t bottom_margin =
        std::clamp(layout.bottom_margin, 0, available_height - height);
    const std::int32_t left = client_rect.right - right_margin - width;
    const std::int32_t top = client_rect.bottom - bottom_margin - height;
    return {left, top, left + width, top + height};
}

bool should_show(const VisibilityContext& context) noexcept {
    if (!context.selected_window_valid || context.selected_window_minimized ||
        !context.client_rect_usable) {
        return false;
    }
    return context.selected_window_is_foreground ||
           (context.interaction_enabled && context.overlay_is_foreground);
}

} // namespace wowai::overlay
