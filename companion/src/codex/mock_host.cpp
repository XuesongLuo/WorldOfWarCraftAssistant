#include "wowai/codex/mock_host.hpp"

namespace wowai::codex {
namespace {
constexpr std::string_view fixed_timestamp = "2000-01-01T00:00:00Z";
constexpr std::string_view ready_message_id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr std::string_view response_message_id = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
} // namespace

nlohmann::json make_deterministic_response(const nlohmann::json& request) {
    return {{"schemaVersion", protocol_version},
            {"requestId", request.at("requestId")},
            {"status", "completed"},
            {"mode", request.at("mode")},
            {"answer",
             {{"summary", "[mock:" + request.at("mode").get<std::string>() + "] " +
                              request.at("question").get<std::string>()},
              {"nextSteps", {"确认问题与角色上下文", "使用正式运行时时重新请求"}},
              {"constraints", {"确定性离线模拟器未连接模型或知识源"}},
              {"uncertainties", nlohmann::json::array()},
              {"followUp", nullptr}}},
            {"sources", nlohmann::json::array()},
            {"usage",
             {{"imageUsed", request.at("images").size() == 1},
              {"knowledgeUsed", false},
              {"runtime", "codex"},
              {"provider", "mock"}}},
            {"error", nullptr}};
}

ValidationResult DeterministicMockHost::handle_line(std::string_view line, nlohmann::json& output) {
    nlohmann::json input;
    if (auto result = parse_json_line(line, input); !result) {
        return result;
    }
    auto envelope = input.get<ProtocolEnvelope>();
    if (auto result = sequence_.accept(envelope); !result) {
        return result;
    }
    if (envelope.kind == "hello") {
        output = {{"protocolVersion", protocol_version},
                  {"messageId", ready_message_id},
                  {"kind", "ready"},
                  {"requestId", nullptr},
                  {"sequence", 1},
                  {"sentAt", fixed_timestamp},
                  {"timeoutMs", nullptr},
                  {"payload", {{"selectedVersion", protocol_version}, {"maxMessageBytes", max_message_bytes}}}};
    } else if (envelope.kind == "request") {
        output = {{"protocolVersion", protocol_version},
                  {"messageId", response_message_id},
                  {"kind", "response"},
                  {"requestId", envelope.request_id},
                  {"sequence", 1},
                  {"sentAt", fixed_timestamp},
                  {"timeoutMs", nullptr},
                  {"payload", make_deterministic_response(envelope.payload)}};
    } else {
        return {false, "mock host only accepts hello and request messages"};
    }
    auto result = validate_envelope(output);
    if (!result) {
        return result;
    }
    return sequence_.accept(output.get<ProtocolEnvelope>());
}

} // namespace wowai::codex
