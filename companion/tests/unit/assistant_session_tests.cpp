#include "wowai/codex/assistant_session.hpp"
#include "wowai/codex/protocol.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("assistant session builds a strict local-only request") {
    const auto request = wowai::codex::make_assistant_request(
        "11111111-1111-4111-8111-111111111111",
        "22222222-2222-4222-8222-222222222222", "2026-10-02T12:00:00Z", "解释这个机制",
        "local-ollama", "qwen3:8b");

    REQUIRE(wowai::codex::validate_assistant_request(request));
    REQUIRE(request.at("runtime").at("provider") == "local-ollama");
    REQUIRE(request.at("runtime").at("model") == "qwen3:8b");
    REQUIRE_FALSE(request.at("runtime").at("allowCloudUpload").get<bool>());
    REQUIRE(request.at("images").empty());
    REQUIRE(request.at("observations").empty());
}

TEST_CASE("assistant session rejects invalid questions before writing to Host") {
    REQUIRE_THROWS_AS(
        wowai::codex::make_assistant_request(
            "11111111-1111-4111-8111-111111111111",
            "22222222-2222-4222-8222-222222222222", "2026-10-02T12:00:00Z", "",
            "local-ollama", "qwen3:8b"),
        std::invalid_argument);
}
