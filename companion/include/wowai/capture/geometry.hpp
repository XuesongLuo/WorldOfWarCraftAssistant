#pragma once

#include <cstdint>

namespace wowai::capture {

struct Point {
    std::int32_t x{};
    std::int32_t y{};

    bool operator==(const Point&) const = default;
};

struct Size {
    std::int32_t width{};
    std::int32_t height{};

    bool operator==(const Size&) const = default;
};

struct Rect {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};

    [[nodiscard]] std::int32_t width() const noexcept { return right - left; }
    [[nodiscard]] std::int32_t height() const noexcept { return bottom - top; }
    [[nodiscard]] bool valid() const noexcept { return width() > 0 && height() > 0; }

    bool operator==(const Rect&) const = default;
};

} // namespace wowai::capture
