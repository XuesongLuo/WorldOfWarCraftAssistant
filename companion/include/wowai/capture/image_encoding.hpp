#pragma once

#include "wowai/capture/anchor_detector.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace wowai::capture {

inline constexpr std::size_t maximum_inline_image_bytes = 700 * 1024;

struct EncodedImage {
    std::vector<std::uint8_t> bytes;
    std::string mime_type;
    std::string sha256;
    std::string base64;
    std::int32_t width{};
    std::int32_t height{};
};

class EncodedImageTooLarge final : public std::runtime_error {
  public:
    EncodedImageTooLarge() : std::runtime_error("encoded screenshot exceeds the inline request limit") {}
};

[[nodiscard]] EncodedImage encode_png(BgraImageView image);
[[nodiscard]] EncodedImage encode_png_bounded(BgraImageView image,
                                              std::int32_t maximum_long_edge = 2048,
                                              std::int32_t minimum_long_edge = 384);

} // namespace wowai::capture
