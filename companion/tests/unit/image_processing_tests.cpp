#include "wowai/capture/image_processing.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {

wowai::capture::CapturedFrame solid(const std::int32_t width, const std::int32_t height,
                                    const std::array<std::uint8_t, 3> rgb) {
    wowai::capture::CapturedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.bgra_pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t offset = 0; offset < frame.bgra_pixels.size(); offset += 4U) {
        frame.bgra_pixels[offset] = rgb[2];
        frame.bgra_pixels[offset + 1] = rgb[1];
        frame.bgra_pixels[offset + 2] = rgb[0];
        frame.bgra_pixels[offset + 3] = 255;
    }
    return frame;
}

void checker(wowai::capture::CapturedFrame& frame, const std::int32_t block,
             const std::array<std::uint8_t, 3> first,
             const std::array<std::uint8_t, 3> second) {
    for (std::int32_t y = 0; y < frame.height; ++y) {
        for (std::int32_t x = 0; x < frame.width; ++x) {
            const auto color = ((x / block) + (y / block)) % 2 == 0 ? first : second;
            const auto offset = (static_cast<std::size_t>(y) * frame.width + x) * 4U;
            frame.bgra_pixels[offset] = color[2];
            frame.bgra_pixels[offset + 1] = color[1];
            frame.bgra_pixels[offset + 2] = color[0];
        }
    }
}

} // namespace

TEST_CASE("image analysis rejects empty black dark and corrupt frames") {
    using enum wowai::capture::ImageValidity;
    CHECK(wowai::capture::analyze_image({}).validity == empty);
    auto black_image = solid(64, 64, {0, 0, 0});
    CHECK(wowai::capture::analyze_image(black_image.view()).validity == black);
    auto dark_image = solid(64, 64, {12, 12, 12});
    CHECK(wowai::capture::analyze_image(dark_image.view()).validity == too_dark);
    CHECK(wowai::capture::analyze_image({dark_image.bgra_pixels, 64, 64, 1}).validity == corrupt);
    auto valid_image = solid(64, 64, {80, 100, 140});
    CHECK(wowai::capture::analyze_image(valid_image.view()).validity == valid);
}

TEST_CASE("crop resize and privacy masking stay inside the selected image") {
    auto frame = solid(100, 50, {80, 120, 160});
    auto cropped = wowai::capture::crop_image(frame.view(), {10, 5, 90, 45});
    CHECK(cropped.width == 80);
    CHECK(cropped.height == 40);
    const std::array masks{wowai::capture::Rect{4, 3, 12, 9}};
    wowai::capture::apply_privacy_masks(cropped, masks);
    const auto masked = (static_cast<std::size_t>(4) * cropped.width + 5U) * 4U;
    CHECK(cropped.bgra_pixels[masked] == 0);
    CHECK(cropped.bgra_pixels[masked + 3] == 255);

    const auto resized = wowai::capture::resize_image(cropped.view(), 40);
    CHECK(resized.width == 40);
    CHECK(resized.height == 20);
    CHECK_THROWS(wowai::capture::crop_image(frame.view(), {-1, 0, 5, 5}));
    CHECK_THROWS(wowai::capture::apply_privacy_masks(cropped,
                                                     std::array{wowai::capture::Rect{0, 0, 90, 5}}));
}

TEST_CASE("local coarse scene classifier returns only the five allowed categories") {
    auto map = solid(160, 100, {190, 150, 80});
    CHECK(wowai::capture::classify_scene(map.view()).kind == wowai::capture::SceneKind::map);

    auto bags = solid(160, 100, {30, 30, 30});
    checker(bags, 3, {25, 25, 25}, {135, 135, 135});
    CHECK(wowai::capture::classify_scene(bags.view()).kind == wowai::capture::SceneKind::bags);

    auto character = solid(160, 100, {45, 70, 150});
    checker(character, 3, {35, 60, 150}, {90, 120, 210});
    CHECK(wowai::capture::classify_scene(character.view()).kind ==
          wowai::capture::SceneKind::character_equipment);

    auto quest = solid(160, 100, {250, 220, 100});
    checker(quest, 3, {250, 220, 100}, {100, 80, 30});
    CHECK(wowai::capture::classify_scene(quest.view()).kind ==
          wowai::capture::SceneKind::quest_log);

    auto unknown = solid(160, 100, {80, 90, 85});
    CHECK(wowai::capture::classify_scene(unknown.view()).kind ==
          wowai::capture::SceneKind::unknown);
}

TEST_CASE("vision fixture matrix covers required resolutions and DPI scales") {
    std::ifstream input(WOWAI_VISION_FIXTURES);
    REQUIRE(input.good());
    const auto fixtures = nlohmann::json::parse(input);
    std::vector<double> scales;
    for (const auto& fixture : fixtures) {
        CAPTURE(fixture.at("name").get<std::string>());
        const auto width = fixture.at("captureWidth").get<std::int32_t>();
        const auto height = fixture.at("captureHeight").get<std::int32_t>();
        scales.push_back(fixture.at("dpiScale").get<double>());
        const auto resized = wowai::capture::resize_image(solid(width, height, {80, 100, 140}).view(),
                                                          2048);
        CHECK(resized.width == fixture.at("expectedWidth").get<std::int32_t>());
        CHECK(resized.height == fixture.at("expectedHeight").get<std::int32_t>());
    }
    CHECK(scales == std::vector<double>{1.0, 1.25, 1.5, 2.0});
}
