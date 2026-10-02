#pragma once

#include "wowai/capture/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace wowai::capture {

struct BgraImageView {
    std::span<const std::uint8_t> pixels;
    std::int32_t width{};
    std::int32_t height{};
    std::int32_t stride{};

    [[nodiscard]] bool valid() const noexcept;
};

struct AnchorDetection {
    Rect anchor_rect;
    Rect secondary_anchor_rect;
    Rect content_rect;
    double scale{};
    double confidence{};
};

struct AnchorDetectorOptions {
    std::int32_t minimum_block_size{6};
    std::int32_t maximum_block_size{24};
    std::uint8_t color_tolerance{36};
    double minimum_confidence{0.90};
};

class AnchorDetector final {
  public:
    explicit AnchorDetector(AnchorDetectorOptions options = {});

    [[nodiscard]] std::optional<AnchorDetection> detect(BgraImageView image) const;

  private:
    AnchorDetectorOptions options_;
};

[[nodiscard]] Rect default_content_rect(const Rect& anchor_rect, double scale) noexcept;
[[nodiscard]] Rect mapped_content_rect(const Rect& top_left_anchor, const Rect& bottom_right_anchor,
                                       double scale) noexcept;

} // namespace wowai::capture
