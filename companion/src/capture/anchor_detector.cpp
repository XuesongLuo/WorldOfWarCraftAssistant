#include "wowai/capture/anchor_detector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace wowai::capture {
namespace {

struct Rgb {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

constexpr std::array<Rgb, 4> top_left_colors{{
    {0, 255, 255},
    {255, 0, 255},
    {255, 255, 255},
    {0, 31, 46},
}};

constexpr std::array<Rgb, 4> bottom_right_colors{{
    {255, 0, 255},
    {0, 255, 255},
    {0, 31, 46},
    {255, 255, 255},
}};

[[nodiscard]] Rgb pixel_at(const BgraImageView image, const std::int32_t x,
                           const std::int32_t y) noexcept {
    const auto offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(image.stride) +
                        static_cast<std::size_t>(x) * 4U;
    return {image.pixels[offset + 2], image.pixels[offset + 1], image.pixels[offset]};
}

[[nodiscard]] double color_score(const Rgb actual, const Rgb expected,
                                 const std::uint8_t tolerance) noexcept {
    const auto difference = [](const std::uint8_t lhs, const std::uint8_t rhs) {
        return std::abs(static_cast<int>(lhs) - static_cast<int>(rhs));
    };
    const int maximum_difference =
        std::max({difference(actual.red, expected.red), difference(actual.green, expected.green),
                  difference(actual.blue, expected.blue)});
    if (maximum_difference > tolerance) {
        return 0.0;
    }
    return 1.0 - static_cast<double>(maximum_difference) /
                     static_cast<double>(std::max<std::uint8_t>(tolerance, 1));
}

[[nodiscard]] double quadrant_score(const BgraImageView image, const std::int32_t left,
                                    const std::int32_t top, const std::int32_t block_size,
                                    const Rgb expected, const std::uint8_t tolerance) noexcept {
    const std::array<std::int32_t, 3> offsets{
        std::max(0, block_size / 5),
        std::max(0, block_size / 2),
        std::max(0, block_size - 1 - block_size / 5),
    };
    double sum = 0.0;
    for (const std::int32_t y : offsets) {
        for (const std::int32_t x : offsets) {
            sum += color_score(pixel_at(image, left + x, top + y), expected, tolerance);
        }
    }
    return sum / 9.0;
}

[[nodiscard]] double candidate_score(const BgraImageView image, const std::int32_t left,
                                     const std::int32_t top, const std::int32_t block_size,
                                     const std::array<Rgb, 4>& colors,
                                     const std::uint8_t tolerance) noexcept {
    return (quadrant_score(image, left, top, block_size, colors[0], tolerance) +
            quadrant_score(image, left + block_size, top, block_size, colors[1], tolerance) +
            quadrant_score(image, left, top + block_size, block_size, colors[2], tolerance) +
            quadrant_score(image, left + block_size, top + block_size, block_size, colors[3],
                           tolerance)) /
           4.0;
}

struct PatternMatch {
    Rect rect;
    double confidence{};
};

[[nodiscard]] std::optional<PatternMatch>
find_unique_pattern(const BgraImageView image, const std::array<Rgb, 4>& colors,
                    const AnchorDetectorOptions options) noexcept {
    double best_score = 0.0;
    double second_best_score = 0.0;
    Rect best_rect{};
    const auto is_leading_color = [&](const std::int32_t x, const std::int32_t y) {
        return color_score(pixel_at(image, x, y), colors[0], options.color_tolerance) > 0.5;
    };

    for (std::int32_t y = 0; y < image.height; ++y) {
        for (std::int32_t x = 0; x < image.width; ++x) {
            if (!is_leading_color(x, y) || (x > 0 && is_leading_color(x - 1, y)) ||
                (y > 0 && is_leading_color(x, y - 1))) {
                continue;
            }

            std::int32_t horizontal_run = 0;
            while (x + horizontal_run < image.width &&
                   horizontal_run <= options.maximum_block_size &&
                   is_leading_color(x + horizontal_run, y)) {
                ++horizontal_run;
            }
            std::int32_t vertical_run = 0;
            while (y + vertical_run < image.height && vertical_run <= options.maximum_block_size &&
                   is_leading_color(x, y + vertical_run)) {
                ++vertical_run;
            }
            if (horizontal_run < options.minimum_block_size ||
                horizontal_run > options.maximum_block_size ||
                std::abs(horizontal_run - vertical_run) > 1) {
                continue;
            }

            const std::int32_t block = horizontal_run;
            if (x + block * 2 > image.width || y + block * 2 > image.height) {
                continue;
            }
            const double score =
                candidate_score(image, x, y, block, colors, options.color_tolerance);
            if (score > best_score) {
                second_best_score = best_score;
                best_score = score;
                best_rect = {x, y, x + block * 2, y + block * 2};
            } else if (score > second_best_score) {
                second_best_score = score;
            }
        }
    }

    if (best_score < options.minimum_confidence || !best_rect.valid() ||
        second_best_score >= options.minimum_confidence) {
        return std::nullopt;
    }
    return PatternMatch{best_rect, best_score};
}

} // namespace

bool BgraImageView::valid() const noexcept {
    if (width <= 0 || height <= 0 || stride < width * 4) {
        return false;
    }
    const auto required = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
    return pixels.size() >= required;
}

AnchorDetector::AnchorDetector(const AnchorDetectorOptions options) : options_(options) {
    if (options_.minimum_block_size <= 0 ||
        options_.maximum_block_size < options_.minimum_block_size ||
        options_.minimum_confidence < 0.0 || options_.minimum_confidence > 1.0) {
        throw std::invalid_argument("invalid anchor detector options");
    }
}

std::optional<AnchorDetection> AnchorDetector::detect(const BgraImageView image) const {
    if (!image.valid()) {
        return std::nullopt;
    }

    const auto top_left = find_unique_pattern(image, top_left_colors, options_);
    const auto bottom_right = find_unique_pattern(image, bottom_right_colors, options_);
    if (!top_left || !bottom_right ||
        std::abs(top_left->rect.width() - bottom_right->rect.width()) > 2 ||
        bottom_right->rect.left <= top_left->rect.right ||
        bottom_right->rect.top <= top_left->rect.bottom) {
        return std::nullopt;
    }

    const double scale = static_cast<double>(top_left->rect.width()) / 20.0;
    const Rect content = mapped_content_rect(top_left->rect, bottom_right->rect, scale);
    if (content.left < 0 || content.top < 0 || content.right > image.width ||
        content.bottom > image.height || !content.valid()) {
        return std::nullopt;
    }
    return AnchorDetection{top_left->rect, bottom_right->rect, content, scale,
                           std::min(top_left->confidence, bottom_right->confidence)};
}

Rect default_content_rect(const Rect& anchor_rect, const double scale) noexcept {
    const auto scaled = [scale](const double logical) {
        return static_cast<std::int32_t>(std::lround(logical * scale));
    };
    return {
        anchor_rect.left + scaled(5.0),
        anchor_rect.top + scaled(79.0),
        anchor_rect.left + scaled(541.0),
        anchor_rect.top + scaled(403.0),
    };
}

Rect mapped_content_rect(const Rect& top_left_anchor, const Rect& bottom_right_anchor,
                         const double scale) noexcept {
    const auto scaled = [scale](const double logical) {
        return static_cast<std::int32_t>(std::lround(logical * scale));
    };
    return {
        top_left_anchor.left + scaled(5.0),
        top_left_anchor.top + scaled(79.0),
        bottom_right_anchor.right + scaled(19.0),
        bottom_right_anchor.bottom - scaled(3.0),
    };
}

} // namespace wowai::capture
