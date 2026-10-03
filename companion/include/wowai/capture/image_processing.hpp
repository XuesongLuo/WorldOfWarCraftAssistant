#pragma once

#include "wowai/capture/geometry.hpp"
#include "wowai/capture/window_capture.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace wowai::capture {

enum class ImageValidity { valid, empty, black, too_dark, corrupt };

struct ImageAnalysis {
    ImageValidity validity{ImageValidity::corrupt};
    double mean_luminance{};
    double dark_pixel_ratio{};
    double luminance_variance{};
};

[[nodiscard]] ImageAnalysis analyze_image(BgraImageView image) noexcept;
[[nodiscard]] CapturedFrame crop_image(BgraImageView image, Rect region);
void apply_privacy_masks(CapturedFrame& image, std::span<const Rect> masks);
[[nodiscard]] CapturedFrame resize_image(BgraImageView image, std::int32_t maximum_long_edge);

enum class SceneKind { map, bags, quest_log, character_equipment, unknown };

struct SceneClassification {
    SceneKind kind{SceneKind::unknown};
    double confidence{};
};

[[nodiscard]] SceneClassification classify_scene(BgraImageView image) noexcept;
[[nodiscard]] std::string_view to_string(SceneKind kind) noexcept;

} // namespace wowai::capture
