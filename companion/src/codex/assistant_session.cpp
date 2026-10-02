#include "wowai/codex/assistant_session.hpp"

#include "wowai/codex/protocol.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

#include <objbase.h>
#include <windows.h>

namespace wowai::codex {
namespace {

std::string new_uuid() {
    GUID value{};
    if (FAILED(::CoCreateGuid(&value))) {
        throw std::runtime_error("could not create request identifier");
    }
    std::array<char, 37> text{};
    const int written = std::snprintf(
        text.data(), text.size(), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        value.Data1, value.Data2, value.Data3, value.Data4[0], value.Data4[1], value.Data4[2],
        value.Data4[3], value.Data4[4], value.Data4[5], value.Data4[6], value.Data4[7]);
    if (written != 36) {
        throw std::runtime_error("could not format request identifier");
    }
    return text.data();
}

std::string utc_now() {
    SYSTEMTIME value{};
    ::GetSystemTime(&value);
    std::array<char, 25> text{};
    const int written = std::snprintf(text.data(), text.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                                      value.wYear, value.wMonth, value.wDay, value.wHour,
                                      value.wMinute, value.wSecond, value.wMilliseconds);
    if (written != 24) {
        throw std::runtime_error("could not format request timestamp");
    }
    return text.data();
}

std::optional<std::wstring> environment_value(const wchar_t* name) {
    const DWORD size = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) {
        return std::nullopt;
    }
    std::wstring value(size, L'\0');
    const DWORD written = ::GetEnvironmentVariableW(name, value.data(), size);
    if (written == 0 || written >= size) {
        throw std::runtime_error("could not read assistant environment configuration");
    }
    value.resize(written);
    return value;
}

std::string narrow_ascii(const std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        if (character < 0x20 || character > 0x7e) {
            throw std::runtime_error("model configuration must contain printable ASCII");
        }
        result.push_back(static_cast<char>(character));
    }
    return result;
}

nlohmann::json hello() {
    return {{"protocolVersion", protocol_version},
            {"messageId", new_uuid()},
            {"kind", "hello"},
            {"requestId", nullptr},
            {"sequence", 0},
            {"sentAt", utc_now()},
            {"timeoutMs", nullptr},
            {"payload", {{"supportedVersions", {protocol_version}},
                         {"maxMessageBytes", max_message_bytes}}}};
}

} // namespace

AssistantSession::AssistantSession(const HostLaunchOptions& options, std::string provider,
                                   std::string model)
    : client_(options), conversation_id_(new_uuid()), provider_(std::move(provider)),
      model_(std::move(model)) {
    if (provider_ != "local-ollama" || model_.empty() || model_.size() > 128) {
        throw std::invalid_argument("only one explicit local Ollama model is supported");
    }
    const auto ready = client_.negotiate(hello(), std::chrono::seconds{5});
    if (ready.at("kind") != "ready") {
        throw std::runtime_error("Codex Host negotiation failed");
    }
}

std::string AssistantSession::ask(const std::string_view question,
                                  const std::chrono::milliseconds timeout) {
    const std::string request_id = new_uuid();
    auto payload = make_assistant_request(request_id, conversation_id_, utc_now(), question,
                                          provider_, model_);
    nlohmann::json envelope{{"protocolVersion", protocol_version},
                            {"messageId", new_uuid()},
                            {"kind", "request"},
                            {"requestId", request_id},
                            {"sequence", 0},
                            {"sentAt", utc_now()},
                            {"timeoutMs", timeout.count()},
                            {"payload", std::move(payload)}};
    const auto response = client_.request(envelope, timeout + std::chrono::seconds{2});
    if (response.at("kind") == "error") {
        const auto& error = response.at("payload");
        throw std::runtime_error(error.at("code").get<std::string>() + ": " +
                                 error.at("message").get<std::string>());
    }
    return response.at("payload").at("answer").at("summary").get<std::string>();
}

std::unique_ptr<AssistantSession> AssistantSession::from_environment() {
    const auto node = environment_value(L"WOWAI_NODE_BINARY");
    const auto host = environment_value(L"WOWAI_HOST_SCRIPT");
    const auto provider = environment_value(L"WOWAI_MODEL_PROVIDER");
    const auto endpoint = environment_value(L"WOWAI_LOCAL_MODEL_ENDPOINT");
    const auto model = environment_value(L"WOWAI_LOCAL_MODEL");
    const auto codex_binary = environment_value(L"WOWAI_CODEX_BINARY");
    const auto codex_lock = environment_value(L"WOWAI_CODEX_LOCK");
    const auto codex_root = environment_value(L"WOWAI_CODEX_ROOT");
    if (!node && !host && !provider && !endpoint && !model && !codex_binary && !codex_lock &&
        !codex_root) {
        return nullptr;
    }
    if (!node || !host || !provider || !endpoint || !model || !codex_binary || !codex_lock ||
        !codex_root) {
        throw std::runtime_error(
            "WOWAI_NODE_BINARY, WOWAI_HOST_SCRIPT, WOWAI_MODEL_PROVIDER, "
            "WOWAI_LOCAL_MODEL_ENDPOINT, WOWAI_LOCAL_MODEL, WOWAI_CODEX_BINARY, "
            "WOWAI_CODEX_LOCK, and WOWAI_CODEX_ROOT must be configured together");
    }
    const auto working = environment_value(L"WOWAI_HOST_WORKING_DIRECTORY");
    HostLaunchOptions options{std::filesystem::path{*node}, {*host},
                              working ? std::filesystem::path{*working}
                                      : std::filesystem::current_path(),
                              std::chrono::seconds{2}};
    return std::make_unique<AssistantSession>(options, narrow_ascii(*provider),
                                              narrow_ascii(*model));
}

nlohmann::json make_assistant_request(const std::string_view request_id,
                                      const std::string_view conversation_id,
                                      const std::string_view created_at,
                                      const std::string_view question,
                                      const std::string_view provider,
                                      const std::string_view model) {
    nlohmann::json result{
        {"schemaVersion", protocol_version},
        {"requestId", request_id},
        {"conversationId", conversation_id},
        {"createdAt", created_at},
        {"mode", "general"},
        {"locale", "zh-CN"},
        {"gameFlavor", "retail"},
        {"question", question},
        {"character", {{"region", "cn"},
                       {"realm", nullptr},
                       {"name", nullptr},
                       {"classId", nullptr},
                       {"specializationId", nullptr},
                       {"level", nullptr}}},
        {"images", nlohmann::json::array()},
        {"observations", nlohmann::json::array()},
        {"privacy", {{"selectedWindowOnly", true},
                     {"screenObservationEnabled", false},
                     {"rawFramesPersisted", false}}},
        {"client", {{"addonVersion", nullptr},
                    {"companionVersion", "0.1.0-dev"},
                    {"uiScale", nullptr}}},
        {"runtime", {{"engine", "codex"},
                     {"provider", provider},
                     {"model", model},
                     {"allowCloudUpload", false}}}};
    const auto validation = validate_assistant_request(result);
    if (!validation) {
        throw std::invalid_argument(validation.message);
    }
    return result;
}

} // namespace wowai::codex
