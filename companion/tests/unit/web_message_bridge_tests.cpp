#include "wowai/overlay/web_message_bridge.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

TEST_CASE("web bridge accepts only exact allowlisted messages") {
    const auto valid = wowai::overlay::parse_web_message(
        "about:blank", R"({"version":1,"type":"send_message","payload":{"text":"hello"}})");
    REQUIRE(valid.message);
    REQUIRE(valid.message->kind == wowai::overlay::WebMessageKind::send_message);
    REQUIRE(valid.message->text == "hello");

    REQUIRE_FALSE(wowai::overlay::parse_web_message("https://evil.invalid",
                                                    R"({"version":1,"type":"ready","payload":{}})")
                      .message);
    REQUIRE(wowai::overlay::parse_web_message("data:text/html,owned-document",
                                              R"({"version":1,"type":"ready","payload":{}})",
                                              "data:text/html,owned-document")
                .message);
    REQUIRE_FALSE(wowai::overlay::parse_web_message("data:text/html,other",
                                                    R"({"version":1,"type":"ready","payload":{}})",
                                                    "data:text/html,owned-document")
                      .message);
    REQUIRE_FALSE(wowai::overlay::parse_web_message(
                      "about:blank", R"({"version":1,"type":"ready","payload":{},"extra":true})")
                      .message);
    REQUIRE_FALSE(wowai::overlay::parse_web_message(
                      "about:blank", R"({"version":1,"type":"unknown","payload":{}})")
                      .message);
}

TEST_CASE("web bridge rejects oversized and malformed payloads") {
    const std::string oversized(wowai::overlay::maximum_web_message_bytes + 1, 'x');
    REQUIRE_FALSE(wowai::overlay::parse_web_message("about:blank", oversized).message);
    REQUIRE_FALSE(
        wowai::overlay::parse_web_message(
            "about:blank", R"({"version":1,"type":"send_message","payload":{"text":"","x":1}})")
            .message);
}

TEST_CASE("external links use an exact HTTPS host allowlist") {
    REQUIRE(wowai::overlay::is_allowed_external_url("https://support.blizzard.com/article/123"));
    REQUIRE_FALSE(
        wowai::overlay::is_allowed_external_url("http://support.blizzard.com/article/123"));
    REQUIRE_FALSE(
        wowai::overlay::is_allowed_external_url("https://support.blizzard.com.evil.invalid/"));
    REQUIRE_FALSE(wowai::overlay::is_allowed_external_url("https://example.com/"));
}

TEST_CASE("native messages are serialized rather than concatenated") {
    const std::string message = wowai::overlay::make_assistant_message("quote: \" and newline\n");
    REQUIRE(message.find("assistant_message") != std::string::npos);
    REQUIRE(message.find("\\\"") != std::string::npos);
    REQUIRE(message.find("\\n") != std::string::npos);
}

TEST_CASE("vision controls are strict explicit UI messages") {
    const auto capture = wowai::overlay::parse_web_message(
        "about:blank",
        R"({"version":1,"type":"capture_screenshot","payload":{"maskChat":true,"selectedRegion":false}})");
    REQUIRE(capture.message);
    CHECK(capture.message->kind == wowai::overlay::WebMessageKind::capture_screenshot);
    CHECK(capture.message->enabled);
    CHECK_FALSE(capture.message->selected_region);

    const auto coaching = wowai::overlay::parse_web_message(
        "about:blank", R"({"version":1,"type":"set_observation","payload":{"mode":"coaching"}})");
    REQUIRE(coaching.message);
    CHECK(coaching.message->kind == wowai::overlay::WebMessageKind::set_observation);
    CHECK(coaching.message->text == "coaching");
    CHECK_FALSE(
        wowai::overlay::parse_web_message(
            "about:blank", R"({"version":1,"type":"set_observation","payload":{"mode":"hidden"}})")
            .message);
}

TEST_CASE("request cancellation and actionable states are explicit bridge messages") {
    const auto cancel = wowai::overlay::parse_web_message(
        "about:blank", R"({"version":1,"type":"cancel_request","payload":{}})");
    REQUIRE(cancel.message);
    CHECK(cancel.message->kind == wowai::overlay::WebMessageKind::cancel_request);
    CHECK_FALSE(wowai::overlay::parse_web_message(
                    "about:blank", R"({"version":1,"type":"cancel_request","payload":{"id":"x"}})")
                    .message);

    const auto state = wowai::overlay::make_request_state_message("error", "请求超时", "AI_TIMEOUT",
                                                                  true, "检查网络后重试");
    const auto json = nlohmann::json::parse(state);
    CHECK(json.at("phase") == "error");
    CHECK(json.at("errorCode") == "AI_TIMEOUT");
    CHECK(json.at("retryable") == true);
    CHECK(json.at("action") == "检查网络后重试");
}

TEST_CASE("native screenshot preview is a JSON data URL without a filesystem path") {
    const auto message = wowai::overlay::make_screenshot_preview("iVBORw0KGgo=", 320, 180, true);
    CHECK(message.find("data:image/png;base64,iVBORw0KGgo=") != std::string::npos);
    CHECK(message.find("filesystem") == std::string::npos);
    CHECK(message.find("privacyMaskApplied") != std::string::npos);
}

TEST_CASE("native appearance message carries bounded numeric preferences") {
    const auto message = wowai::overlay::make_appearance_message(88, 19);
    const auto json = nlohmann::json::parse(message);
    CHECK(json.at("type") == "appearance");
    CHECK(json.at("opacity") == 88);
    CHECK(json.at("fontSize") == 19);
}
