#include "wowai/capture/image_processing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wowai::capture {
namespace {

[[nodiscard]] bool valid_image(const BgraImageView image) noexcept {
    if (image.width <= 0 || image.height <= 0 || image.stride < image.width * 4) {
        return false;
    }
    const auto required = static_cast<std::uint64_t>(image.stride) *
                          static_cast<std::uint64_t>(image.height);
    return required <= image.pixels.size();
}

[[nodiscard]] double luminance(const std::uint8_t* pixel) noexcept {
    return 0.0722 * pixel[0] + 0.7152 * pixel[1] + 0.2126 * pixel[2];
}

} // namespace

ImageAnalysis analyze_image(const BgraImageView image) noexcept {
    if (!valid_image(image)) {
        return {image.pixels.empty() ? ImageValidity::empty : ImageValidity::corrupt};
    }

    constexpr std::int32_t maximum_samples_per_axis = 256;
    const std::int32_t step_x = std::max(1, image.width / maximum_samples_per_axis);
    const std::int32_t step_y = std::max(1, image.height / maximum_samples_per_axis);
    double sum = 0.0;
    double squared_sum = 0.0;
    std::uint64_t dark_pixels = 0;
    std::uint64_t samples = 0;
    for (std::int32_t y = 0; y < image.height; y += step_y) {
        const auto* row = image.pixels.data() + static_cast<std::size_t>(y) * image.stride;
        for (std::int32_t x = 0; x < image.width; x += step_x) {
            const double value = luminance(row + static_cast<std::size_t>(x) * 4U);
            sum += value;
            squared_sum += value * value;
            dark_pixels += value < 12.0 ? 1U : 0U;
            ++samples;
        }
    }
    if (samples == 0) {
        return {ImageValidity::empty};
    }
    const double count = static_cast<double>(samples);
    const double mean = sum / count;
    const double variance = std::max(0.0, squared_sum / count - mean * mean);
    const double dark_ratio = static_cast<double>(dark_pixels) / count;
    if (dark_ratio >= 0.995 && mean < 3.0 && variance < 2.0) {
        return {ImageValidity::black, mean, dark_ratio, variance};
    }
    if (mean < 18.0 || dark_ratio > 0.97) {
        return {ImageValidity::too_dark, mean, dark_ratio, variance};
    }
    return {ImageValidity::valid, mean, dark_ratio, variance};
}

CapturedFrame crop_image(const BgraImageView image, const Rect region) {
    if (!valid_image(image) || !region.valid() || region.left < 0 || region.top < 0 ||
        region.right > image.width || region.bottom > image.height) {
        throw std::invalid_argument("crop region must be inside a valid BGRA image");
    }
    CapturedFrame result;
    result.width = region.width();
    result.height = region.height();
    result.bgra_pixels.resize(static_cast<std::size_t>(result.width) * result.height * 4U);
    for (std::int32_t y = 0; y < result.height; ++y) {
        const auto* source = image.pixels.data() +
                             static_cast<std::size_t>(region.top + y) * image.stride +
                             static_cast<std::size_t>(region.left) * 4U;
        auto* destination = result.bgra_pixels.data() +
                            static_cast<std::size_t>(y) * result.width * 4U;
        std::copy_n(source, static_cast<std::size_t>(result.width) * 4U, destination);
    }
    return result;
}

void apply_privacy_masks(CapturedFrame& image, const std::span<const Rect> masks) {
    if (!valid_image(image.view())) {
        throw std::invalid_argument("privacy masks require a valid BGRA image");
    }
    for (const Rect mask : masks) {
        if (!mask.valid() || mask.left < 0 || mask.top < 0 || mask.right > image.width ||
            mask.bottom > image.height) {
            throw std::invalid_argument("privacy mask must be inside the captured image");
        }
        for (std::int32_t y = mask.top; y < mask.bottom; ++y) {
            for (std::int32_t x = mask.left; x < mask.right; ++x) {
                auto* pixel = image.bgra_pixels.data() +
                              (static_cast<std::size_t>(y) * image.width + x) * 4U;
                pixel[0] = 0;
                pixel[1] = 0;
                pixel[2] = 0;
                pixel[3] = 255;
            }
        }
    }
}

CapturedFrame resize_image(const BgraImageView image, const std::int32_t maximum_long_edge) {
    if (!valid_image(image) || maximum_long_edge <= 0) {
        throw std::invalid_argument("resize requires a valid image and positive edge limit");
    }
    const std::int32_t long_edge = std::max(image.width, image.height);
    if (long_edge <= maximum_long_edge) {
        return crop_image(image, {0, 0, image.width, image.height});
    }
    const double scale = static_cast<double>(maximum_long_edge) / long_edge;
    const auto width = std::max(1, static_cast<std::int32_t>(std::lround(image.width * scale)));
    const auto height = std::max(1, static_cast<std::int32_t>(std::lround(image.height * scale)));
    CapturedFrame result;
    result.width = width;
    result.height = height;
    result.bgra_pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    for (std::int32_t y = 0; y < height; ++y) {
        const auto source_y = std::min(image.height - 1, static_cast<std::int32_t>(y / scale));
        for (std::int32_t x = 0; x < width; ++x) {
            const auto source_x = std::min(image.width - 1, static_cast<std::int32_t>(x / scale));
            const auto* source = image.pixels.data() +
                                 static_cast<std::size_t>(source_y) * image.stride +
                                 static_cast<std::size_t>(source_x) * 4U;
            auto* destination = result.bgra_pixels.data() +
                                (static_cast<std::size_t>(y) * width + x) * 4U;
            std::copy_n(source, 4U, destination);
        }
    }
    return result;
}

SceneClassification classify_scene(const BgraImageView image) noexcept {
    const ImageAnalysis analysis = analyze_image(image);
    if (analysis.validity != ImageValidity::valid || image.width < 32 || image.height < 32) {
        return {};
    }

    std::uint64_t warm = 0;
    std::uint64_t blue = 0;
    std::uint64_t bright = 0;
    std::uint64_t edges = 0;
    std::uint64_t samples = 0;
    const std::int32_t step = std::max(1, std::min(image.width, image.height) / 160);
    for (std::int32_t y = step; y < image.height; y += step) {
        const auto* row = image.pixels.data() + static_cast<std::size_t>(y) * image.stride;
        const auto* previous = image.pixels.data() + static_cast<std::size_t>(y - step) * image.stride;
        for (std::int32_t x = step; x < image.width; x += step) {
            const auto* pixel = row + static_cast<std::size_t>(x) * 4U;
            const auto* left = row + static_cast<std::size_t>(x - step) * 4U;
            const auto* above = previous + static_cast<std::size_t>(x) * 4U;
            warm += pixel[2] > pixel[0] * 1.25 && pixel[1] > pixel[0] * 0.75 ? 1U : 0U;
            blue += pixel[0] > pixel[2] * 1.25 ? 1U : 0U;
            bright += luminance(pixel) > 175.0 ? 1U : 0U;
            const int horizontal = std::abs(static_cast<int>(luminance(pixel) - luminance(left)));
            const int vertical = std::abs(static_cast<int>(luminance(pixel) - luminance(above)));
            edges += horizontal > 45 || vertical > 45 ? 1U : 0U;
            ++samples;
        }
    }
    if (samples == 0) {
        return {};
    }
    const double warm_ratio = static_cast<double>(warm) / samples;
    const double blue_ratio = static_cast<double>(blue) / samples;
    const double bright_ratio = static_cast<double>(bright) / samples;
    const double edge_ratio = static_cast<double>(edges) / samples;

    if (warm_ratio > 0.55 && edge_ratio < 0.22) {
        return {SceneKind::map, std::min(0.95, 0.55 + warm_ratio * 0.4)};
    }
    if (warm_ratio > 0.30 && bright_ratio > 0.24 && edge_ratio >= 0.18) {
        return {SceneKind::quest_log, std::min(0.90, 0.45 + warm_ratio + bright_ratio * 0.2)};
    }
    if (blue_ratio > 0.22 && edge_ratio > 0.18) {
        return {SceneKind::character_equipment, std::min(0.88, 0.45 + blue_ratio + edge_ratio * 0.2)};
    }
    if (edge_ratio > 0.42 && analysis.mean_luminance < 115.0) {
        return {SceneKind::bags, std::min(0.92, 0.48 + edge_ratio)};
    }
    return {SceneKind::unknown, std::min(0.49, 0.15 + std::abs(edge_ratio - 0.25))};
}

std::string_view to_string(const SceneKind kind) noexcept {
    switch (kind) {
    case SceneKind::map:
        return "map";
    case SceneKind::bags:
        return "bags";
    case SceneKind::quest_log:
        return "quest-log";
    case SceneKind::character_equipment:
        return "character-equipment";
    case SceneKind::unknown:
        return "unknown";
    }
    return "unknown";
}

} // namespace wowai::capture
