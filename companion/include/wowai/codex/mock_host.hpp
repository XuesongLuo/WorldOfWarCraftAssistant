#pragma once

#include "wowai/codex/protocol.hpp"

namespace wowai::codex {

nlohmann::json make_deterministic_response(const nlohmann::json& request);

class DeterministicMockHost {
  public:
    ValidationResult handle_line(std::string_view line, nlohmann::json& output);

  private:
    ProtocolSequenceTracker sequence_;
};

} // namespace wowai::codex
