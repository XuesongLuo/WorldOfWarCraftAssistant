#include "wowai/capture/anchor_detector.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;

struct Sample {
    std::string name;
    std::int32_t width;
    std::int32_t height;
    double scale;
    std::int32_t block;
    std::int32_t x;
    std::int32_t y;
    std::int32_t bottom_x;
    std::int32_t bottom_y;
    wowai::capture::Rect expected_content;
};

std::vector<Sample> load_samples() {
    std::ifstream input(WOWAI_ANCHOR_FIXTURES);
    REQUIRE(input.good());
    const json fixtures = json::parse(input);
    std::vector<Sample> samples;
    for (const auto& fixture : fixtures) {
        const auto& expected = fixture.at("expectedContent");
        samples.push_back(
            {fixture.at("name").get<std::string>(),
             fixture.at("screenWidth").get<std::int32_t>(),
             fixture.at("screenHeight").get<std::int32_t>(),
             fixture.at("uiScale").get<double>(),
             fixture.at("blockSize").get<std::int32_t>(),
             fixture.at("anchorX").get<std::int32_t>(),
             fixture.at("anchorY").get<std::int32_t>(),
             fixture.at("bottomAnchorX").get<std::int32_t>(),
             fixture.at("bottomAnchorY").get<std::int32_t>(),
             {expected.at(0).get<std::int32_t>(), expected.at(1).get<std::int32_t>(),
              expected.at(2).get<std::int32_t>(), expected.at(3).get<std::int32_t>()}});
    }
    return samples;
}

void fill_rect(std::vector<std::uint8_t>& pixels, const std::int32_t width, const std::int32_t left,
               const std::int32_t top, const std::int32_t size,
               const std::array<std::uint8_t, 3> rgb) {
    for (std::int32_t y = top; y < top + size; ++y) {
        for (std::int32_t x = left; x < left + size; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            pixels[offset] = rgb[2];
            pixels[offset + 1] = rgb[1];
            pixels[offset + 2] = rgb[0];
            pixels[offset + 3] = 255;
        }
    }
}

std::vector<std::uint8_t> make_image(const Sample& sample, const bool complete_marker = true) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(sample.width) * sample.height * 4U,
                                     255);
    fill_rect(pixels, sample.width, sample.x, sample.y, sample.block, {0, 255, 255});
    fill_rect(pixels, sample.width, sample.x + sample.block, sample.y, sample.block, {255, 0, 255});
    fill_rect(pixels, sample.width, sample.x, sample.y + sample.block, sample.block,
              {255, 255, 255});
    if (complete_marker) {
        fill_rect(pixels, sample.width, sample.x + sample.block, sample.y + sample.block,
                  sample.block, {0, 31, 46});
        fill_rect(pixels, sample.width, sample.bottom_x, sample.bottom_y, sample.block,
                  {255, 0, 255});
        fill_rect(pixels, sample.width, sample.bottom_x + sample.block, sample.bottom_y,
                  sample.block, {0, 255, 255});
        fill_rect(pixels, sample.width, sample.bottom_x, sample.bottom_y + sample.block,
                  sample.block, {0, 31, 46});
        fill_rect(pixels, sample.width, sample.bottom_x + sample.block,
                  sample.bottom_y + sample.block, sample.block, {255, 255, 255});
    }
    return pixels;
}

} // namespace

TEST_CASE("anchor detector recognizes the synthetic resolution and UI scale matrix") {
    const wowai::capture::AnchorDetector detector;
    for (const Sample& sample : load_samples()) {
        CAPTURE(sample.name);
        const auto pixels = make_image(sample);
        const auto detection = detector.detect(
            {pixels, sample.width, sample.height, static_cast<std::int32_t>(sample.width * 4)});

        REQUIRE(detection);
        CHECK(detection->anchor_rect == wowai::capture::Rect{sample.x, sample.y,
                                                             sample.x + sample.block * 2,
                                                             sample.y + sample.block * 2});
        CHECK(detection->confidence >= 0.99);
        CHECK(std::abs(detection->scale - sample.scale) <= 0.001);
        CHECK(detection->secondary_anchor_rect ==
              wowai::capture::Rect{sample.bottom_x, sample.bottom_y,
                                   sample.bottom_x + sample.block * 2,
                                   sample.bottom_y + sample.block * 2});
        CHECK(detection->content_rect == sample.expected_content);
    }
}

TEST_CASE("anchor detector fails closed for incomplete or invalid images") {
    const wowai::capture::AnchorDetector detector;
    const Sample sample{"incomplete", 800, 600, 1.0, 10, 40, 50, 566, 436, {45, 129, 605, 453}};
    const auto pixels = make_image(sample, false);

    CHECK_FALSE(detector.detect({pixels, sample.width, sample.height, sample.width * 4}));
    CHECK_FALSE(detector.detect({pixels, sample.width, sample.height, 1}));
}

TEST_CASE("anchor detector fails closed when more than one marker is present") {
    const wowai::capture::AnchorDetector detector;
    const Sample first{"ambiguous", 1200, 900, 1.0, 10, 40, 50, 566, 436, {45, 129, 605, 453}};
    const Sample second{"ambiguous", 1200, 900,  1.0, 10,
                        620,         100,  1146, 486, {625, 179, 1185, 503}};
    auto pixels = make_image(first);
    const auto second_pixels = make_image(second);
    for (std::size_t index = 0; index < pixels.size(); index += 4) {
        if (second_pixels[index] != 255 || second_pixels[index + 1] != 255 ||
            second_pixels[index + 2] != 255) {
            pixels[index] = second_pixels[index];
            pixels[index + 1] = second_pixels[index + 1];
            pixels[index + 2] = second_pixels[index + 2];
        }
    }

    CHECK_FALSE(detector.detect({pixels, first.width, first.height, first.width * 4}));
}
