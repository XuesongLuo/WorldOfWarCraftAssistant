#pragma once

#include "wowai/capture/geometry.hpp"

#include <optional>

namespace wowai::capture {

struct NormalizedRect {
    double left{};
    double top{};
    double right{};
    double bottom{};

    [[nodiscard]] bool valid() const noexcept;
};

class ManualCalibration final {
  public:
    [[nodiscard]] bool set(const Rect& content_rect, const Size& client_size) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] std::optional<Rect> resolve(const Size& client_size) const noexcept;
    [[nodiscard]] std::optional<NormalizedRect> normalized_rect() const noexcept;

  private:
    std::optional<NormalizedRect> rect_;
};

} // namespace wowai::capture
