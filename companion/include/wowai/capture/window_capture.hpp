#pragma once

#include "wowai/capture/anchor_detector.hpp"

#include <cstdint>
#include <vector>

#include <windows.h>

namespace wowai::capture {

struct CapturedFrame {
    std::vector<std::uint8_t> bgra_pixels;
    std::int32_t width{};
    std::int32_t height{};

    [[nodiscard]] BgraImageView view() const noexcept;
};

class WindowCapture final {
  public:
    [[nodiscard]] CapturedFrame capture_client(HWND window) const;
};

} // namespace wowai::capture
