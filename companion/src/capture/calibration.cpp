#include "wowai/capture/calibration.hpp"

#include <algorithm>
#include <cmath>

namespace wowai::capture {

bool NormalizedRect::valid() const noexcept {
    return left >= 0.0 && top >= 0.0 && right <= 1.0 && bottom <= 1.0 && left < right &&
           top < bottom;
}

bool ManualCalibration::set(const Rect& content_rect, const Size& client_size) noexcept {
    if (!content_rect.valid() || client_size.width <= 0 || client_size.height <= 0 ||
        content_rect.left < 0 || content_rect.top < 0 || content_rect.right > client_size.width ||
        content_rect.bottom > client_size.height) {
        return false;
    }
    rect_ = NormalizedRect{
        static_cast<double>(content_rect.left) / client_size.width,
        static_cast<double>(content_rect.top) / client_size.height,
        static_cast<double>(content_rect.right) / client_size.width,
        static_cast<double>(content_rect.bottom) / client_size.height,
    };
    return true;
}

void ManualCalibration::reset() noexcept { rect_.reset(); }

bool ManualCalibration::configured() const noexcept { return rect_.has_value(); }

std::optional<Rect> ManualCalibration::resolve(const Size& client_size) const noexcept {
    if (!rect_ || !rect_->valid() || client_size.width <= 0 || client_size.height <= 0) {
        return std::nullopt;
    }
    const auto scaled = [](const double value, const std::int32_t extent) {
        return static_cast<std::int32_t>(std::lround(value * extent));
    };
    return Rect{scaled(rect_->left, client_size.width), scaled(rect_->top, client_size.height),
                scaled(rect_->right, client_size.width), scaled(rect_->bottom, client_size.height)};
}

std::optional<NormalizedRect> ManualCalibration::normalized_rect() const noexcept { return rect_; }

} // namespace wowai::capture
