#include "wowai/capture/visual_bridge.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>

namespace wowai::capture {
namespace {

constexpr std::size_t header_size = 19;
constexpr std::size_t checksum_size = 4;
constexpr std::size_t cells_per_row = 64;
constexpr std::array<std::array<std::uint8_t, 3>, 16> palette{{
    {13, 13, 13},   {204, 13, 13}, {13, 204, 13},  {204, 204, 13},
    {13, 13, 204},  {204, 13, 204}, {13, 204, 204}, {204, 204, 204},
    {89, 89, 89},   {255, 51, 51},  {51, 255, 51},  {255, 255, 51},
    {51, 51, 255},  {255, 51, 255}, {51, 255, 255}, {255, 255, 255},
}};

struct FieldRule {
    std::string_view key;
    std::uint32_t bit;
};

constexpr std::array field_rules{
    FieldRule{"class", 1U << 0U},          FieldRule{"classId", 1U << 1U},
    FieldRule{"specialization", 1U << 2U}, FieldRule{"specializationId", 1U << 3U},
    FieldRule{"level", 1U << 4U},          FieldRule{"zone", 1U << 5U},
    FieldRule{"mapId", 1U << 6U},          FieldRule{"activity", 1U << 7U},
    FieldRule{"encounterId", 1U << 8U},    FieldRule{"achievementId", 1U << 9U},
    FieldRule{"criteria", 1U << 10U},      FieldRule{"event", 1U << 11U},
    FieldRule{"skills", 1U << 12U},        FieldRule{"talents", 1U << 13U},
    FieldRule{"actionSlots", 1U << 14U},   FieldRule{"keyBindings", 1U << 15U},
    FieldRule{"unavailable", 1U << 31U},
};
constexpr std::uint32_t known_field_bits = 0x8000FFFFU;

[[nodiscard]] std::uint16_t read_u16(const std::span<const std::uint8_t> bytes,
                                     const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                      bytes[offset + 1]);
}

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::uint8_t> bytes,
                                     const std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) | bytes[offset + 3];
}

[[nodiscard]] std::optional<std::uint32_t> bit_for(const std::string_view key) noexcept {
    for (const auto& rule : field_rules) {
        if (rule.key == key) {
            return rule.bit;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool hex_value(const char character, std::uint8_t& value) noexcept {
    if (character >= '0' && character <= '9') {
        value = static_cast<std::uint8_t>(character - '0');
        return true;
    }
    if (character >= 'A' && character <= 'F') {
        value = static_cast<std::uint8_t>(character - 'A' + 10);
        return true;
    }
    return false;
}

[[nodiscard]] std::optional<std::string> percent_decode(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '%') {
            const auto byte = static_cast<unsigned char>(value[index]);
            if (byte < 0x20 || value[index] == '&' || value[index] == '=') {
                return std::nullopt;
            }
            result.push_back(value[index]);
            continue;
        }
        if (index + 2 >= value.size()) {
            return std::nullopt;
        }
        std::uint8_t high{};
        std::uint8_t low{};
        if (!hex_value(value[index + 1], high) || !hex_value(value[index + 2], low)) {
            return std::nullopt;
        }
        const char decoded = static_cast<char>((high << 4U) | low);
        if (decoded == '\0') {
            return std::nullopt;
        }
        result.push_back(decoded);
        index += 2;
    }
    return result;
}

[[nodiscard]] std::optional<std::uint8_t> palette_index(const std::uint8_t* bgra) noexcept {
    std::uint32_t best_distance = std::numeric_limits<std::uint32_t>::max();
    std::uint8_t best{};
    for (std::uint8_t index = 0; index < palette.size(); ++index) {
        const int blue = static_cast<int>(bgra[0]) - palette[index][2];
        const int green = static_cast<int>(bgra[1]) - palette[index][1];
        const int red = static_cast<int>(bgra[2]) - palette[index][0];
        const auto distance = static_cast<std::uint32_t>(blue * blue + green * green + red * red);
        if (distance < best_distance) {
            best_distance = distance;
            best = index;
        }
    }
    if (best_distance > 32U * 32U * 3U) {
        return std::nullopt;
    }
    return best;
}

[[nodiscard]] const std::uint8_t* pixel(BgraImageView image, const std::int32_t x,
                                        const std::int32_t y) noexcept {
    return image.pixels.data() + static_cast<std::size_t>(y) * image.stride +
           static_cast<std::size_t>(x) * 4U;
}

[[nodiscard]] std::optional<std::uint8_t> sample_nibble(BgraImageView image, const std::int32_t x,
                                                        const std::int32_t y,
                                                        const std::int32_t cell,
                                                        const std::size_t index) noexcept {
    const auto column = static_cast<std::int32_t>(index % cells_per_row);
    const auto row = static_cast<std::int32_t>(index / cells_per_row);
    const std::int32_t sample_x = x + column * cell + cell / 2;
    const std::int32_t sample_y = y + row * cell + cell / 2;
    if (sample_x < 0 || sample_y < 0 || sample_x >= image.width || sample_y >= image.height) {
        return std::nullopt;
    }
    return palette_index(pixel(image, sample_x, sample_y));
}

[[nodiscard]] std::optional<std::uint8_t> sample_byte(BgraImageView image, const std::int32_t x,
                                                      const std::int32_t y,
                                                      const std::int32_t cell,
                                                      const std::size_t byte_index) noexcept {
    const auto high = sample_nibble(image, x, y, cell, byte_index * 2U);
    const auto low = sample_nibble(image, x, y, cell, byte_index * 2U + 1U);
    if (!high || !low) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>((*high << 4U) | *low);
}

} // namespace

std::uint32_t visual_bridge_crc32(const std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t value = 0xFFFFFFFFU;
    for (const std::uint8_t byte : bytes) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value >> 1U) ^ (0xEDB88320U & (0U - (value & 1U)));
        }
    }
    return ~value;
}

VisualBridgeResult decode_visual_bridge_frame(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < header_size + checksum_size) {
        return {{}, VisualBridgeError::truncated};
    }
    if (bytes[0] != 'W' || bytes[1] != 'A' || bytes[2] != 'I') {
        return {{}, VisualBridgeError::bad_magic};
    }
    if (bytes[3] != visual_bridge_protocol_version) {
        return {{}, VisualBridgeError::unknown_version};
    }
    if (bytes[4] != visual_bridge_plugin_public_source) {
        return {{}, VisualBridgeError::unknown_source};
    }
    const std::uint32_t bitmap = read_u32(bytes, 13);
    if ((bitmap & ~known_field_bits) != 0) {
        return {{}, VisualBridgeError::unknown_field};
    }
    const std::size_t payload_length = read_u16(bytes, 17);
    if (payload_length > visual_bridge_max_payload_bytes) {
        return {{}, VisualBridgeError::oversized};
    }
    const std::size_t expected_size = header_size + payload_length + checksum_size;
    if (bytes.size() != expected_size) {
        return {{}, VisualBridgeError::truncated};
    }
    if (visual_bridge_crc32(bytes.first(expected_size - checksum_size)) !=
        read_u32(bytes, expected_size - checksum_size)) {
        return {{}, VisualBridgeError::crc_mismatch};
    }

    VisualBridgeFrame frame{bytes[3], bytes[4], read_u32(bytes, 5), read_u32(bytes, 9), bitmap};
    const std::string_view payload{reinterpret_cast<const char*>(bytes.data() + header_size),
                                   payload_length};
    std::uint32_t observed_bitmap = 0;
    std::size_t position = 0;
    while (position < payload.size()) {
        const std::size_t end = payload.find('&', position);
        const std::string_view entry = payload.substr(position, end - position);
        const std::size_t separator = entry.find('=');
        if (separator == std::string_view::npos || separator == 0) {
            return {{}, VisualBridgeError::malformed_payload};
        }
        const std::string_view key = entry.substr(0, separator);
        const auto bit = bit_for(key);
        const auto decoded = percent_decode(entry.substr(separator + 1));
        if (!bit) {
            return {{}, VisualBridgeError::unknown_field};
        }
        if (!decoded || frame.fields.contains(std::string{key}) || decoded->size() > 384) {
            return {{}, VisualBridgeError::malformed_payload};
        }
        observed_bitmap |= *bit;
        if (key == "unavailable") {
            std::size_t item_start = 0;
            while (item_start < decoded->size()) {
                const std::size_t item_end = decoded->find(',', item_start);
                const std::string item = decoded->substr(item_start, item_end - item_start);
                if (!bit_for(item) || item == "unavailable") {
                    return {{}, VisualBridgeError::malformed_payload};
                }
                frame.unavailable_fields.push_back(item);
                if (item_end == std::string::npos) {
                    break;
                }
                item_start = item_end + 1;
            }
        }
        frame.fields.emplace(std::string{key}, *decoded);
        if (end == std::string_view::npos) {
            break;
        }
        position = end + 1;
    }
    if (observed_bitmap != bitmap) {
        return {{}, VisualBridgeError::field_bitmap_mismatch};
    }
    return {std::move(frame), VisualBridgeError::none};
}

std::optional<std::vector<std::uint8_t>> sample_visual_bridge(const BgraImageView image,
                                                              Rect& sampled_rect) {
    if (image.width <= 0 || image.height <= 0 || image.stride < image.width * 4 ||
        static_cast<std::uint64_t>(image.stride) * image.height > image.pixels.size()) {
        return std::nullopt;
    }
    constexpr std::array<std::uint8_t, 4> prefix{'W', 'A', 'I', visual_bridge_protocol_version};
    for (std::int32_t cell = 2; cell <= 8; ++cell) {
        const std::int32_t minimum_width = static_cast<std::int32_t>(cells_per_row) * cell;
        if (minimum_width > image.width) {
            continue;
        }
        for (std::int32_t y = 0; y + cell <= image.height; ++y) {
            for (std::int32_t x = 0; x + minimum_width <= image.width; ++x) {
                bool match = true;
                for (std::size_t index = 0; index < prefix.size(); ++index) {
                    const auto value = sample_byte(image, x, y, cell, index);
                    if (!value || *value != prefix[index]) {
                        match = false;
                        break;
                    }
                }
                if (!match) {
                    continue;
                }
                std::array<std::uint8_t, header_size> header{};
                for (std::size_t index = 0; index < header.size(); ++index) {
                    const auto value = sample_byte(image, x, y, cell, index);
                    if (!value) {
                        match = false;
                        break;
                    }
                    header[index] = *value;
                }
                if (!match) {
                    continue;
                }
                const std::size_t frame_size = header_size + read_u16(header, 17) + checksum_size;
                if (frame_size > header_size + visual_bridge_max_payload_bytes + checksum_size) {
                    continue;
                }
                const std::size_t nibbles = frame_size * 2U;
                const std::int32_t rows = static_cast<std::int32_t>((nibbles + cells_per_row - 1U) /
                                                                    cells_per_row);
                if (y + rows * cell > image.height) {
                    continue;
                }
                std::vector<std::uint8_t> frame(frame_size);
                for (std::size_t index = 0; index < frame_size; ++index) {
                    const auto value = sample_byte(image, x, y, cell, index);
                    if (!value) {
                        match = false;
                        break;
                    }
                    frame[index] = *value;
                }
                if (match && decode_visual_bridge_frame(frame).frame) {
                    sampled_rect = {x, y, x + minimum_width, y + rows * cell};
                    return frame;
                }
            }
        }
    }
    return std::nullopt;
}

VisualBridgeError VisualBridgeGate::accept(const VisualBridgeFrame& frame,
                                           const std::uint32_t now_unix) {
    if (frame.captured_at_unix > now_unix + 2U || now_unix > frame.captured_at_unix + 5U) {
        return VisualBridgeError::expired;
    }
    if (last_sequence_ && frame.sequence <= *last_sequence_) {
        return VisualBridgeError::duplicate_or_old;
    }
    while (!accepted_times_.empty() && accepted_times_.front() + 1U <= now_unix) {
        accepted_times_.pop_front();
    }
    if (accepted_times_.size() >= 10U) {
        return VisualBridgeError::rate_limited;
    }
    last_sequence_ = frame.sequence;
    accepted_times_.push_back(now_unix);
    return VisualBridgeError::none;
}

void VisualBridgeGate::reset() noexcept {
    last_sequence_.reset();
    accepted_times_.clear();
}

std::string_view to_string(const VisualBridgeError error) noexcept {
    switch (error) {
    case VisualBridgeError::none: return "none";
    case VisualBridgeError::not_found: return "not-found";
    case VisualBridgeError::invalid_palette: return "invalid-palette";
    case VisualBridgeError::truncated: return "truncated";
    case VisualBridgeError::bad_magic: return "bad-magic";
    case VisualBridgeError::unknown_version: return "unknown-version";
    case VisualBridgeError::unknown_source: return "unknown-source";
    case VisualBridgeError::oversized: return "oversized";
    case VisualBridgeError::crc_mismatch: return "crc-mismatch";
    case VisualBridgeError::malformed_payload: return "malformed-payload";
    case VisualBridgeError::unknown_field: return "unknown-field";
    case VisualBridgeError::field_bitmap_mismatch: return "field-bitmap-mismatch";
    case VisualBridgeError::duplicate_or_old: return "duplicate-or-old";
    case VisualBridgeError::expired: return "expired";
    case VisualBridgeError::rate_limited: return "rate-limited";
    }
    return "unknown";
}

} // namespace wowai::capture
