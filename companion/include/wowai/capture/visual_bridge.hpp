#pragma once

#include "wowai/capture/anchor_detector.hpp"
#include "wowai/capture/geometry.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wowai::capture {

inline constexpr std::uint8_t visual_bridge_protocol_version = 1;
inline constexpr std::size_t visual_bridge_max_payload_bytes = 512;
inline constexpr std::uint8_t visual_bridge_plugin_public_source = 1;

struct VisualBridgeFrame {
    std::uint8_t protocol_version{};
    std::uint8_t source{};
    std::uint32_t sequence{};
    std::uint32_t captured_at_unix{};
    std::uint32_t field_bitmap{};
    std::map<std::string, std::string, std::less<>> fields;
    std::vector<std::string> unavailable_fields;
};

enum class VisualBridgeError {
    none,
    not_found,
    invalid_palette,
    truncated,
    bad_magic,
    unknown_version,
    unknown_source,
    oversized,
    crc_mismatch,
    malformed_payload,
    unknown_field,
    field_bitmap_mismatch,
    duplicate_or_old,
    expired,
    rate_limited,
};

struct VisualBridgeResult {
    std::optional<VisualBridgeFrame> frame;
    VisualBridgeError error{VisualBridgeError::none};
};

[[nodiscard]] std::uint32_t visual_bridge_crc32(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] VisualBridgeResult decode_visual_bridge_frame(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> sample_visual_bridge(BgraImageView image,
                                                                           Rect& sampled_rect);

class VisualBridgeGate final {
  public:
    [[nodiscard]] VisualBridgeError accept(const VisualBridgeFrame& frame,
                                           std::uint32_t now_unix);
    void reset() noexcept;

  private:
    std::optional<std::uint32_t> last_sequence_;
    std::deque<std::uint32_t> accepted_times_;
};

[[nodiscard]] std::string_view to_string(VisualBridgeError error) noexcept;

} // namespace wowai::capture
