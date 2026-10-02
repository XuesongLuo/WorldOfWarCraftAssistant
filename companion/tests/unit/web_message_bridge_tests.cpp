#include "wowai/overlay/web_message_bridge.hpp"

#include <catch2/catch_test_macros.hpp>

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
