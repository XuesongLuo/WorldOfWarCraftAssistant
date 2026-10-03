#include "wowai/capture/image_encoding.hpp"
#include "wowai/capture/window_capture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

TEST_CASE("WIC PNG encoding stays in memory and records digest and inline content") {
    wowai::capture::CapturedFrame frame;
    frame.width = 32;
    frame.height = 24;
    frame.bgra_pixels.resize(32U * 24U * 4U);
    for (std::size_t offset = 0; offset < frame.bgra_pixels.size(); offset += 4U) {
        frame.bgra_pixels[offset] = static_cast<std::uint8_t>((offset / 4U) % 251U);
        frame.bgra_pixels[offset + 1U] = 90;
        frame.bgra_pixels[offset + 2U] = 180;
        frame.bgra_pixels[offset + 3U] = 255;
    }
    const auto encoded = wowai::capture::encode_png(frame.view());
    REQUIRE(encoded.bytes.size() > 8);
    CHECK(encoded.bytes[0] == 0x89);
    CHECK(encoded.bytes[1] == 'P');
    CHECK(encoded.bytes[2] == 'N');
    CHECK(encoded.bytes[3] == 'G');
    CHECK(encoded.mime_type == "image/png");
    CHECK(encoded.sha256.size() == 64);
    CHECK_FALSE(encoded.base64.empty());
    CHECK(encoded.bytes.size() <= wowai::capture::maximum_inline_image_bytes);
}

TEST_CASE("WIC PNG encoder rejects malformed image views") {
    CHECK_THROWS(wowai::capture::encode_png({{}, 10, 10, 40}));
    CHECK_THROWS(wowai::capture::encode_png_bounded({{}, 10, 10, 40}));
    CHECK_THROWS(wowai::capture::encode_png_bounded({}, 128, 256));
}

TEST_CASE("bounded PNG encoding reduces complex images to the inline limit") {
    wowai::capture::CapturedFrame frame;
    frame.width = 1024;
    frame.height = 1024;
    frame.bgra_pixels.resize(static_cast<std::size_t>(frame.width) * frame.height * 4U);
    std::uint32_t noise = 0x12345678U;
    for (auto& byte : frame.bgra_pixels) {
        noise = noise * 1664525U + 1013904223U;
        byte = static_cast<std::uint8_t>(noise >> 24U);
    }
    const auto encoded = wowai::capture::encode_png_bounded(frame.view(), 1024, 384);
    CHECK(encoded.bytes.size() <= wowai::capture::maximum_inline_image_bytes);
    CHECK(encoded.width < 1024);
    CHECK(encoded.width >= 384);
    CHECK(encoded.height == encoded.width);
}
