#include "wowai/codex/mock_host.hpp"
#include "wowai/codex/protocol.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

namespace {

nlohmann::json load_fixtures() {
    std::ifstream stream(WOWAI_CONTRACT_FIXTURES);
    REQUIRE(stream.good());
    return nlohmann::json::parse(stream);
}

nlohmann::json valid_request() {
    const auto fixtures = load_fixtures();
    for (const auto& item : fixtures.at("assistantRequests")) {
        if (item.at("accepted").get<bool>()) {
            return item.at("value");
        }
    }
    throw std::runtime_error("shared fixtures contain no valid request");
}

nlohmann::json hello_envelope() {
    return {{"protocolVersion", wowai::codex::protocol_version},
            {"messageId", "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"},
            {"kind", "hello"},
            {"requestId", nullptr},
            {"sequence", 0},
            {"sentAt", "2026-09-29T12:00:00Z"},
            {"timeoutMs", nullptr},
            {"payload", {{"supportedVersions", {wowai::codex::protocol_version}}, {"maxMessageBytes", wowai::codex::max_message_bytes}}}};
}

nlohmann::json ready_envelope() {
    return {{"protocolVersion", wowai::codex::protocol_version},
            {"messageId", "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"},
            {"kind", "ready"},
            {"requestId", nullptr},
            {"sequence", 1},
            {"sentAt", "2026-09-29T12:00:00Z"},
            {"timeoutMs", nullptr},
            {"payload", {{"selectedVersion", wowai::codex::protocol_version}, {"maxMessageBytes", wowai::codex::max_message_bytes}}}};
}

nlohmann::json request_envelope() {
    auto request = valid_request();
    return {{"protocolVersion", wowai::codex::protocol_version},
            {"messageId", "cccccccc-cccc-4ccc-8ccc-cccccccccccc"},
            {"kind", "request"},
            {"requestId", request.at("requestId")},
            {"sequence", 0},
            {"sentAt", "2026-09-29T12:00:00Z"},
            {"timeoutMs", wowai::codex::default_timeout_ms},
            {"payload", std::move(request)}};
}

} // namespace

TEST_CASE("C++ agrees with shared AssistantRequest fixtures", "[codex][contract]") {
    const auto fixtures = load_fixtures();
    for (const auto& item : fixtures.at("assistantRequests")) {
        CAPTURE(item.at("name"));
        REQUIRE(static_cast<bool>(wowai::codex::validate_assistant_request(item.at("value"))) ==
                item.at("accepted").get<bool>());
    }
}

TEST_CASE("C++ agrees with shared AssistantResponse fixtures", "[codex][contract]") {
    const auto fixtures = load_fixtures();
    for (const auto& item : fixtures.at("assistantResponses")) {
        CAPTURE(item.at("name"));
        REQUIRE(static_cast<bool>(wowai::codex::validate_assistant_response(item.at("value"))) ==
                item.at("accepted").get<bool>());
    }
}

TEST_CASE("JSONL parsing rejects malformed UTF-8 and oversized messages", "[codex][boundary]") {
    nlohmann::json parsed;
    const std::string malformed{"\xc3\x28", 2};
    REQUIRE_FALSE(wowai::codex::parse_json_line(malformed, parsed));
    const std::string oversized(wowai::codex::max_message_bytes + 1, ' ');
    REQUIRE_FALSE(wowai::codex::parse_json_line(oversized, parsed));
}

TEST_CASE("envelope rejects version mismatch", "[codex][contract]") {
    auto hello = hello_envelope();
    hello["protocolVersion"] = "3.0";
    REQUIRE_FALSE(wowai::codex::validate_envelope(hello));
}

TEST_CASE("session rejects an out-of-order response", "[codex][session]") {
    wowai::codex::ProtocolSequenceTracker tracker;
    REQUIRE(tracker.accept(hello_envelope().get<wowai::codex::ProtocolEnvelope>()));
    REQUIRE(tracker.accept(ready_envelope().get<wowai::codex::ProtocolEnvelope>()));
    REQUIRE(tracker.accept(request_envelope().get<wowai::codex::ProtocolEnvelope>()));

    auto response = nlohmann::json{{"protocolVersion", wowai::codex::protocol_version},
                                   {"messageId", "dddddddd-dddd-4ddd-8ddd-dddddddddddd"},
                                   {"kind", "response"},
                                   {"requestId", valid_request().at("requestId")},
                                   {"sequence", 2},
                                   {"sentAt", "2026-09-29T12:00:00Z"},
                                   {"timeoutMs", nullptr},
                                   {"payload", wowai::codex::make_deterministic_response(valid_request())}};
    const auto validation = wowai::codex::validate_envelope(response);
    CAPTURE(validation.message);
    REQUIRE(validation);
    REQUIRE_FALSE(tracker.accept(response.get<wowai::codex::ProtocolEnvelope>()));
}

TEST_CASE("mock host is deterministic and model-free", "[codex][mock]") {
    wowai::codex::DeterministicMockHost first;
    wowai::codex::DeterministicMockHost second;
    nlohmann::json first_ready;
    nlohmann::json second_ready;
    const auto first_hello = first.handle_line(hello_envelope().dump(), first_ready);
    CAPTURE(first_hello.message);
    REQUIRE(first_hello);
    REQUIRE(second.handle_line(hello_envelope().dump(), second_ready));
    REQUIRE(first_ready == second_ready);

    nlohmann::json first_response;
    nlohmann::json second_response;
    const auto request_line = request_envelope().dump();
    REQUIRE(first.handle_line(request_line, first_response));
    REQUIRE(second.handle_line(request_line, second_response));
    REQUIRE(first_response == second_response);
    REQUIRE(wowai::codex::validate_envelope(first_response));
}
