#include "wowai/codex/host_client.hpp"
#include "wowai/codex/host_process.hpp"
#include "wowai/codex/protocol.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <windows.h>

namespace {
using namespace std::chrono_literals;

wowai::codex::HostLaunchOptions fixture(std::wstring mode) {
    return {std::filesystem::path{WOWAI_HOST_PROCESS_FIXTURE}, {std::move(mode)},
            std::filesystem::path{WOWAI_REPOSITORY_ROOT}, 100ms};
}

nlohmann::json fixtures() {
    std::ifstream stream(WOWAI_CONTRACT_FIXTURES);
    REQUIRE(stream.good());
    return nlohmann::json::parse(stream);
}

nlohmann::json request_payload() {
    const auto all_fixtures = fixtures();
    for (const auto& item : all_fixtures.at("assistantRequests")) {
        if (item.at("accepted").get<bool>()) return item.at("value");
    }
    throw std::runtime_error("no valid request fixture");
}

nlohmann::json hello() {
    return {{"protocolVersion", wowai::codex::protocol_version},
            {"messageId", "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"},
            {"kind", "hello"},
            {"requestId", nullptr},
            {"sequence", 0},
            {"sentAt", "2026-10-01T12:00:00Z"},
            {"timeoutMs", nullptr},
            {"payload", {{"supportedVersions", {wowai::codex::protocol_version}},
                         {"maxMessageBytes", wowai::codex::max_message_bytes}}}};
}

nlohmann::json request() {
    auto payload = request_payload();
    return {{"protocolVersion", wowai::codex::protocol_version},
            {"messageId", "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"},
            {"kind", "request"},
            {"requestId", payload.at("requestId")},
            {"sequence", 0},
            {"sentAt", "2026-10-01T12:00:00Z"},
            {"timeoutMs", wowai::codex::default_timeout_ms},
            {"payload", std::move(payload)}};
}

} // namespace

TEST_CASE("Host process reassembles split lines and preserves sticky lines", "[codex][process]") {
    {
        wowai::codex::HostProcess process(fixture(L"--split"));
        process.write_line("split-message");
        const auto line = process.read_line(1s);
        REQUIRE(line.status == wowai::codex::HostReadStatus::line);
        REQUIRE(line.line == "split-message");
    }
    {
        wowai::codex::HostProcess process(fixture(L"--sticky"));
        process.write_line("go");
        REQUIRE(process.read_line(1s).line == "first");
        REQUIRE(process.read_line(1s).line == "second");
    }
}

TEST_CASE("Host process reports timeout and crash independently", "[codex][process]") {
    {
        wowai::codex::HostProcess process(fixture(L"--silent"));
        process.write_line("wait");
        REQUIRE(process.read_line(25ms).status == wowai::codex::HostReadStatus::timeout);
    }
    {
        wowai::codex::HostProcess process(fixture(L"--crash"));
        REQUIRE(process.read_line(1s).status == wowai::codex::HostReadStatus::exited);
    }
}

TEST_CASE("closing the Host job terminates descendants", "[codex][process]") {
    DWORD descendant_id{};
    wowai::platform::UniqueHandle descendant;
    {
        wowai::codex::HostProcess process(fixture(L"--spawn-descendant"));
        const auto line = process.read_line(1s);
        REQUIRE(line.status == wowai::codex::HostReadStatus::line);
        descendant_id = static_cast<DWORD>(std::stoul(line.line));
        descendant.reset(::OpenProcess(SYNCHRONIZE, FALSE, descendant_id));
        REQUIRE(descendant);
    }
    REQUIRE(::WaitForSingleObject(descendant.get(), 2'000) == WAIT_OBJECT_0);
}

TEST_CASE("C++ client negotiates with the packaged TypeScript Host", "[codex][integration]") {
    const wowai::codex::HostLaunchOptions options{
        std::filesystem::path{WOWAI_NODE_EXECUTABLE},
        {std::filesystem::path{WOWAI_CODEX_HOST_BUNDLE}.native()},
        std::filesystem::path{WOWAI_REPOSITORY_ROOT},
        1s,
    };
    wowai::codex::HostClient client(options);
    const auto ready = client.negotiate(hello(), 2s);
    REQUIRE(ready.at("kind") == "ready");
    const auto response = client.request(request(), 2s);
    REQUIRE(response.at("kind") == "response");
    REQUIRE(response.at("payload").at("status") == "completed");
    REQUIRE(response.at("payload").at("usage").at("provider") == "mock");
    REQUIRE(client.process().take_stderr().empty());
}
