#include "wowai/capture/visual_bridge.hpp"
#include "wowai/capture/window_capture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

void append_u16(std::vector<std::uint8_t>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

std::vector<std::uint8_t> make_frame(const std::uint32_t sequence = 7,
                                     const std::uint32_t captured_at = 100) {
    const std::string payload = "class=MAGE&level=80&mapId=2339";
    std::vector<std::uint8_t> frame{'W', 'A', 'I', 1, 1};
    append_u32(frame, sequence);
    append_u32(frame, captured_at);
    append_u32(frame, (1U << 0U) | (1U << 4U) | (1U << 6U));
    append_u16(frame, static_cast<std::uint16_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());
    append_u32(frame, wowai::capture::visual_bridge_crc32(frame));
    return frame;
}

constexpr std::array<std::array<std::uint8_t, 3>, 16> palette{{
    {13, 13, 13},   {204, 13, 13}, {13, 204, 13},  {204, 204, 13},
    {13, 13, 204},  {204, 13, 204}, {13, 204, 204}, {204, 204, 204},
    {89, 89, 89},   {255, 51, 51},  {51, 255, 51},  {255, 255, 51},
    {51, 51, 255},  {255, 51, 255}, {51, 255, 255}, {255, 255, 255},
}};

wowai::capture::CapturedFrame render(const std::vector<std::uint8_t>& frame,
                                     const std::int32_t cell) {
    wowai::capture::CapturedFrame image;
    image.width = 64 * cell + 20;
    const auto rows = static_cast<std::int32_t>((frame.size() * 2U + 63U) / 64U);
    image.height = rows * cell + 18;
    image.bgra_pixels.assign(static_cast<std::size_t>(image.width) * image.height * 4U, 0);
    for (std::size_t nibble = 0; nibble < frame.size() * 2U; ++nibble) {
        const auto value = nibble % 2U == 0 ? frame[nibble / 2U] >> 4U : frame[nibble / 2U] & 15U;
        const auto& color = palette[value];
        const auto left = 7 + static_cast<std::int32_t>(nibble % 64U) * cell;
        const auto top = 5 + static_cast<std::int32_t>(nibble / 64U) * cell;
        for (std::int32_t y = top; y < top + cell; ++y) {
            for (std::int32_t x = left; x < left + cell; ++x) {
                const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4U;
                image.bgra_pixels[offset] = color[2];
                image.bgra_pixels[offset + 1] = color[1];
                image.bgra_pixels[offset + 2] = color[0];
                image.bgra_pixels[offset + 3] = 255;
            }
        }
    }
    return image;
}

} // namespace

TEST_CASE("visual bridge decoder accepts a strict public frame") {
    const auto decoded = wowai::capture::decode_visual_bridge_frame(make_frame());
    REQUIRE(decoded.frame);
    CHECK(decoded.frame->protocol_version == 1);
    CHECK(decoded.frame->source == wowai::capture::visual_bridge_plugin_public_source);
    CHECK(decoded.frame->sequence == 7);
    CHECK(decoded.frame->captured_at_unix == 100);
    CHECK(decoded.frame->fields.at("class") == "MAGE");
    CHECK(decoded.frame->fields.at("level") == "80");
}

TEST_CASE("visual bridge rejects corruption unknown fields and bitmap mismatch") {
    auto crc = make_frame();
    crc.back() ^= 1U;
    CHECK(wowai::capture::decode_visual_bridge_frame(crc).error ==
          wowai::capture::VisualBridgeError::crc_mismatch);

    auto unknown_version = make_frame();
    unknown_version[3] = 2;
    CHECK(wowai::capture::decode_visual_bridge_frame(unknown_version).error ==
          wowai::capture::VisualBridgeError::unknown_version);

    auto wrong_bitmap = make_frame();
    wrong_bitmap[16] |= 1U << 1U;
    const auto checksum = wowai::capture::visual_bridge_crc32(
        std::span{wrong_bitmap}.first(wrong_bitmap.size() - 4U));
    wrong_bitmap.resize(wrong_bitmap.size() - 4U);
    append_u32(wrong_bitmap, checksum);
    CHECK(wowai::capture::decode_visual_bridge_frame(wrong_bitmap).error ==
          wowai::capture::VisualBridgeError::field_bitmap_mismatch);
}

TEST_CASE("visual bridge pixels decode across UI scales") {
    for (const std::int32_t cell : {2, 3, 5, 8}) {
        CAPTURE(cell);
        const auto expected = make_frame();
        const auto image = render(expected, cell);
        wowai::capture::Rect sampled{};
        const auto actual = wowai::capture::sample_visual_bridge(image.view(), sampled);
        REQUIRE(actual);
        CHECK(*actual == expected);
        CHECK(sampled.left <= 7);
        CHECK(sampled.left + cell > 7);
        CHECK(sampled.top <= 5);
        CHECK(sampled.top + cell > 5);
    }
}

TEST_CASE("visual bridge gate rejects duplicate stale future and over ten frames per second") {
    wowai::capture::VisualBridgeGate gate;
    auto frame = wowai::capture::decode_visual_bridge_frame(make_frame(1, 100)).frame.value();
    CHECK(gate.accept(frame, 100) == wowai::capture::VisualBridgeError::none);
    CHECK(gate.accept(frame, 100) == wowai::capture::VisualBridgeError::duplicate_or_old);
    frame.sequence = 2;
    frame.captured_at_unix = 90;
    CHECK(gate.accept(frame, 100) == wowai::capture::VisualBridgeError::expired);
    frame.captured_at_unix = 103;
    CHECK(gate.accept(frame, 100) == wowai::capture::VisualBridgeError::expired);

    gate.reset();
    for (std::uint32_t sequence = 1; sequence <= 10; ++sequence) {
        frame.sequence = sequence;
        frame.captured_at_unix = 200;
        CHECK(gate.accept(frame, 200) == wowai::capture::VisualBridgeError::none);
    }
    frame.sequence = 11;
    CHECK(gate.accept(frame, 200) == wowai::capture::VisualBridgeError::rate_limited);
}
