#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace wowai::codex {

inline constexpr char protocol_version[] = "1.0";
inline constexpr std::size_t max_message_bytes = 1'048'576;
inline constexpr std::uint32_t default_timeout_ms = 30'000;
inline constexpr std::uint32_t max_timeout_ms = 120'000;

struct CharacterContext {
    std::string region;
    std::optional<std::string> realm;
    std::optional<std::string> name;
    std::optional<std::uint32_t> class_id;
    std::optional<std::uint32_t> specialization_id;
    std::optional<std::uint32_t> level;
};

struct ImageContext {
    std::string id;
    std::string mime_type;
    std::string capture_scope;
    std::string sha256;
    bool privacy_mask_applied{};
    bool user_confirmed{};
};

struct AssistantRequest {
    std::string schema_version;
    std::string request_id;
    std::string conversation_id;
    std::string created_at;
    std::string mode;
    std::string locale;
    std::string game_flavor;
    std::string question;
    CharacterContext character;
    std::vector<ImageContext> images;
    nlohmann::json client;
    nlohmann::json runtime;
};

struct AssistantError {
    std::string code;
    std::string message;
    bool retryable{};
};

struct AssistantResponse {
    std::string schema_version;
    std::string request_id;
    std::string status;
    std::string mode;
    nlohmann::json answer;
    nlohmann::json sources;
    nlohmann::json usage;
    std::optional<AssistantError> error;
};

struct ProtocolEnvelope {
    std::string protocol_version;
    std::string message_id;
    std::string kind;
    std::optional<std::string> request_id;
    std::uint64_t sequence{};
    std::string sent_at;
    std::optional<std::uint32_t> timeout_ms;
    nlohmann::json payload;
};

struct ValidationResult {
    bool accepted{};
    std::string message;

    explicit operator bool() const noexcept { return accepted; }
};

ValidationResult validate_assistant_request(const nlohmann::json& value);
ValidationResult validate_assistant_response(const nlohmann::json& value);
ValidationResult validate_envelope(const nlohmann::json& value);
ValidationResult parse_json_line(std::string_view line, nlohmann::json& value);

void from_json(const nlohmann::json& value, AssistantRequest& request);
void from_json(const nlohmann::json& value, AssistantError& error);
void from_json(const nlohmann::json& value, AssistantResponse& response);
void from_json(const nlohmann::json& value, ProtocolEnvelope& envelope);

class ProtocolSequenceTracker {
  public:
    ValidationResult accept(const ProtocolEnvelope& envelope);

  private:
    enum class NegotiationState { new_session, hello_received, ready };
    NegotiationState negotiation_state_{NegotiationState::new_session};
    std::unordered_map<std::string, std::uint64_t> next_by_request_;
};

} // namespace wowai::codex
