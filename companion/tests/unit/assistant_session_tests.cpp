#include "wowai/codex/assistant_session.hpp"
#include "wowai/codex/protocol.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

TEST_CASE("assistant session builds an explicitly enabled cloud request") {
    const auto request = wowai::codex::make_assistant_request(
        "11111111-1111-4111-8111-111111111111",
        "22222222-2222-4222-8222-222222222222", "2026-10-02T12:00:00Z", "解释这个机制",
        "openai", "vision-model-fixture");

    REQUIRE(wowai::codex::validate_assistant_request(request));
    REQUIRE(request.at("runtime").at("provider") == "openai");
    REQUIRE(request.at("runtime").at("model") == "vision-model-fixture");
    REQUIRE(request.at("runtime").at("allowCloudUpload").get<bool>());
    REQUIRE(request.at("images").empty());
    REQUIRE(request.at("observations").empty());
}

TEST_CASE("assistant session rejects invalid questions before writing to Host") {
    REQUIRE_THROWS_AS(
        wowai::codex::make_assistant_request(
            "11111111-1111-4111-8111-111111111111",
            "22222222-2222-4222-8222-222222222222", "2026-10-02T12:00:00Z", "",
            "openai", "vision-model-fixture"),
        std::invalid_argument);
}

TEST_CASE("assistant session carries only confirmed inline vision data and versioned provenance") {
    wowai::codex::ConfirmedImage image{
        "44444444-4444-4444-8444-444444444444", "image/png", "selected-region",
        std::string(64, 'a'), "iVBORw0KGgo=", true, "2026-10-02T12:00:01Z"};
    nlohmann::json observations = nlohmann::json::array({
        {{"id", "55555555-5555-4555-8555-555555555555"},
         {"source", "plugin-public"},
         {"kind", "build"},
         {"capturedAt", "2026-10-02T12:00:00Z"},
         {"confidence", 1.0},
         {"summary", "class=MAGE"}},
    });
    nlohmann::json bridge{{"protocolVersion", 1},
                          {"source", "plugin-public"},
                          {"sequence", 7},
                          {"capturedAt", "2026-10-02T12:00:00Z"},
                          {"confidence", 1.0},
                          {"allowedFields", {"class"}},
                          {"unavailableFields", nlohmann::json::array()}};
    auto request = wowai::codex::make_assistant_request(
        "11111111-1111-4111-8111-111111111111",
        "22222222-2222-4222-8222-222222222222", "2026-10-02T12:00:00Z",
        "解释截图", "deepseek", "deepseek-flash", image, observations, bridge, true);

    REQUIRE(wowai::codex::validate_assistant_request(request));
    REQUIRE(request.at("images").at(0).at("userConfirmed") == true);
    REQUIRE(request.at("images").at(0).at("dataBase64") == "iVBORw0KGgo=");
    REQUIRE(request.at("images").at(0).at("uploadDestination") == "deepseek");
    REQUIRE(request.at("runtime").at("provider") == "deepseek");
    REQUIRE(request.at("images").at(0).at("uploadPurpose") == "visual-question");
    REQUIRE(request.at("images").at(0).at("uploadConfirmedAt") ==
            "2026-10-02T12:00:01Z");
    REQUIRE(request.at("visualBridge").at("sequence") == 7);
    REQUIRE(request.at("privacy").at("screenObservationEnabled") == true);

    request["images"][0]["uploadDestination"] = "openai";
    REQUIRE_FALSE(wowai::codex::validate_assistant_request(request));
    request["images"][0]["uploadDestination"] = "deepseek";
    request["images"][0]["userConfirmed"] = false;
    REQUIRE_FALSE(wowai::codex::validate_assistant_request(request));
}

TEST_CASE("assistant session can replace and renegotiate its owned Host safely") {
    using namespace std::chrono_literals;
    const wowai::codex::HostLaunchOptions options{
        std::filesystem::path{WOWAI_HOST_PROCESS_FIXTURE},
        {L"--handshake"},
        std::filesystem::path{WOWAI_REPOSITORY_ROOT},
        100ms,
    };
    wowai::codex::AssistantSession session{options, "openai", "model-fixture"};
    REQUIRE_NOTHROW(session.recover());
}
