#include "wowai/codex/assistant_session.hpp"

#include "wowai/codex/protocol.hpp"
#include "wowai/storage/credential_store.hpp"
#include "wowai/storage/settings.hpp"

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
        text.data(), text.size(), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", value.Data1,
        value.Data2, value.Data3, value.Data4[0], value.Data4[1], value.Data4[2], value.Data4[3],
        value.Data4[4], value.Data4[5], value.Data4[6], value.Data4[7]);
    if (written != 36) {
        throw std::runtime_error("could not format request identifier");
    }
    return text.data();
}

std::string utc_now() {
    SYSTEMTIME value{};
    ::GetSystemTime(&value);
    std::array<char, 25> text{};
    const int written = std::snprintf(
        text.data(), text.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", value.wYear, value.wMonth,
        value.wDay, value.wHour, value.wMinute, value.wSecond, value.wMilliseconds);
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
            {"payload",
             {{"supportedVersions", {protocol_version}}, {"maxMessageBytes", max_message_bytes}}}};
}

nlohmann::json cancel_request(const std::string_view request_id) {
    return {{"protocolVersion", protocol_version},
            {"messageId", new_uuid()},
            {"kind", "cancel"},
            {"requestId", request_id},
            {"sequence", 1},
            {"sentAt", utc_now()},
            {"timeoutMs", nullptr},
            {"payload", {{"reason", "user"}}}};
}

bool cloud_provider(const std::string_view provider) {
    return provider == "openai" || provider == "deepseek" || provider == "xai" ||
           provider == "openrouter" || provider == "dashscope" || provider == "azure-openai";
}

std::vector<std::wstring> provider_credentials() {
    return {L"WOWAI_ACTIVE_API_KEY", L"OPENAI_API_KEY",     L"DEEPSEEK_API_KEY",
            L"XAI_API_KEY",          L"OPENROUTER_API_KEY", L"DASHSCOPE_API_KEY",
            L"AZURE_OPENAI_API_KEY", L"ANTHROPIC_API_KEY",  L"GEMINI_API_KEY",
            L"MISTRAL_API_KEY"};
}

std::optional<std::wstring> legacy_credential(const std::string_view provider) {
    if (provider == "openai")
        return environment_value(L"OPENAI_API_KEY");
    if (provider == "deepseek")
        return environment_value(L"DEEPSEEK_API_KEY");
    if (provider == "xai")
        return environment_value(L"XAI_API_KEY");
    if (provider == "openrouter")
        return environment_value(L"OPENROUTER_API_KEY");
    if (provider == "dashscope")
        return environment_value(L"DASHSCOPE_API_KEY");
    if (provider == "azure-openai")
        return environment_value(L"AZURE_OPENAI_API_KEY");
    return std::nullopt;
}

HostLaunchOptions configured_host_options(const std::wstring& node, const std::wstring& host,
                                          const std::wstring& credential,
                                          const wowai::storage::AssistantSettings& settings) {
    const auto working = environment_value(L"WOWAI_HOST_WORKING_DIRECTORY");
    HostLaunchOptions options{std::filesystem::path{node},
                              {host},
                              working ? std::filesystem::path{*working}
                                      : std::filesystem::current_path(),
                              std::chrono::seconds{2}};
    options.environment_remove = provider_credentials();
    options.environment_overrides = {
        {L"WOWAI_MODEL_PROVIDER",
         std::wstring(settings.cloud_provider.begin(), settings.cloud_provider.end())},
        {L"WOWAI_CLOUD_MODEL",
         std::wstring(settings.cloud_model.begin(), settings.cloud_model.end())},
        {L"WOWAI_CLOUD_UPLOAD_CONSENT", L"1"},
        {L"WOWAI_ACTIVE_API_KEY", credential},
    };
    const auto add = [&options](const wchar_t* name, const std::string& value) {
        if (!value.empty())
            options.environment_overrides.emplace_back(name,
                                                       std::wstring(value.begin(), value.end()));
    };
    add(L"WOWAI_CLOUD_ORGANIZATION", settings.cloud_organization);
    add(L"WOWAI_DASHSCOPE_REGION", settings.cloud_region);
    if (settings.cloud_provider == "dashscope")
        add(L"WOWAI_DASHSCOPE_WORKSPACE", settings.cloud_resource);
    if (settings.cloud_provider == "azure-openai") {
        add(L"WOWAI_AZURE_RESOURCE", settings.cloud_resource);
        add(L"WOWAI_AZURE_API_VERSION", settings.cloud_api_version);
    }
    return options;
}

std::string configured_destination_host(const wowai::storage::AssistantSettings& settings) {
    if (settings.cloud_provider == "openai")
        return "api.openai.com";
    if (settings.cloud_provider == "deepseek")
        return "api.deepseek.com";
    if (settings.cloud_provider == "xai")
        return "api.x.ai";
    if (settings.cloud_provider == "openrouter")
        return "openrouter.ai";
    if (settings.cloud_provider == "azure-openai")
        return settings.cloud_resource + ".openai.azure.com";
    if (settings.cloud_provider == "dashscope") {
        const std::array<std::pair<std::string_view, std::string_view>, 6> regions{{
            {"beijing", "cn-beijing.maas.aliyuncs.com"},
            {"hongkong", "cn-hongkong.maas.aliyuncs.com"},
            {"singapore", "ap-southeast-1.maas.aliyuncs.com"},
            {"tokyo", "ap-northeast-1.maas.aliyuncs.com"},
            {"frankfurt", "eu-central-1.maas.aliyuncs.com"},
            {"virginia", "us-east-1.maas.aliyuncs.com"},
        }};
        for (const auto& [region, suffix] : regions) {
            if (settings.cloud_region == region)
                return settings.cloud_resource + "." + std::string{suffix};
        }
    }
    throw std::invalid_argument("cloud destination is not allowlisted");
}

} // namespace

AssistantSession::AssistantSession(const HostLaunchOptions& options, std::string provider,
                                   std::string model, std::string destination_host)
    : options_(options), conversation_id_(new_uuid()), provider_(std::move(provider)),
      model_(std::move(model)), destination_host_(std::move(destination_host)) {
    if (!cloud_provider(provider_) || model_.empty() || model_.size() > 128) {
        throw std::invalid_argument("one supported cloud provider and explicit model are required");
    }
    connect();
}

void AssistantSession::connect() {
    auto replacement = std::make_unique<HostClient>(options_);
    const auto ready = replacement->negotiate(hello(), std::chrono::seconds{5});
    if (ready.at("kind") != "ready") {
        throw std::runtime_error("Codex Host negotiation failed");
    }
    client_ = std::move(replacement);
}

std::string AssistantSession::ask(const std::string_view question,
                                  const std::chrono::milliseconds timeout,
                                  std::optional<ConfirmedImage> image, nlohmann::json observations,
                                  nlohmann::json visual_bridge,
                                  const bool screen_observation_enabled,
                                  const std::stop_token stop_token, std::string request_id) {
    if (stop_token.stop_requested()) {
        throw AssistantRequestCancelled{};
    }
    if (!client_) {
        throw AssistantFailure("CODEX_START_FAILED", "The Codex Host is unavailable.", true, true);
    }
    if (request_id.empty())
        request_id = new_uuid();
    auto payload = make_assistant_request(
        request_id, conversation_id_, utc_now(), question, provider_, model_, std::move(image),
        std::move(observations), std::move(visual_bridge), screen_observation_enabled);
    nlohmann::json envelope{{"protocolVersion", protocol_version},
                            {"messageId", new_uuid()},
                            {"kind", "request"},
                            {"requestId", request_id},
                            {"sequence", 0},
                            {"sentAt", utc_now()},
                            {"timeoutMs", timeout.count()},
                            {"payload", std::move(payload)}};
    {
        std::scoped_lock lock(active_mutex_);
        active_request_id_ = request_id;
    }
    const auto clear_active = [this, &request_id] {
        std::scoped_lock lock(active_mutex_);
        if (active_request_id_ == request_id) {
            active_request_id_.reset();
        }
    };
    if (stop_token.stop_requested()) {
        clear_active();
        throw AssistantRequestCancelled{};
    }
    try {
        const auto response =
            client_->request(envelope, timeout + std::chrono::seconds{2}, stop_token);
        clear_active();
        if (stop_token.stop_requested()) {
            throw AssistantRequestCancelled{};
        }
        if (response.at("kind") == "error") {
            const auto& error = response.at("payload");
            const std::string code = error.at("code").get<std::string>();
            throw AssistantFailure(code, error.at("message").get<std::string>(),
                                   error.at("retryable").get<bool>(),
                                   code == "CODEX_START_FAILED" || code == "CODEX_PROTOCOL_ERROR");
        }
        return response.at("payload").at("answer").at("summary").get<std::string>();
    } catch (const HostClientFailure& error) {
        clear_active();
        if (error.kind() == HostClientFailureKind::cancelled) {
            throw AssistantRequestCancelled{};
        }
        if (error.kind() == HostClientFailureKind::timeout) {
            throw AssistantFailure("AI_TIMEOUT", "The Codex Host stopped responding.", true, true);
        }
        if (error.kind() == HostClientFailureKind::exited) {
            throw AssistantFailure("CODEX_START_FAILED", "The Codex Host exited unexpectedly.",
                                   true, true);
        }
        throw AssistantFailure("CODEX_PROTOCOL_ERROR",
                               "The Codex Host returned an invalid protocol message.", false, true);
    } catch (const AssistantFailure&) {
        clear_active();
        throw;
    } catch (const AssistantRequestCancelled&) {
        clear_active();
        throw;
    } catch (const std::exception&) {
        clear_active();
        throw AssistantFailure("CODEX_PROTOCOL_ERROR",
                               "The Codex Host returned an invalid protocol structure.", false,
                               true);
    } catch (...) {
        clear_active();
        throw AssistantFailure("CODEX_PROTOCOL_ERROR",
                               "The Codex Host request failed unexpectedly.", false, true);
    }
}

void AssistantSession::cancel_active_request() noexcept {
    try {
        std::scoped_lock lock(active_mutex_);
        if (!active_request_id_ || !client_) {
            return;
        }
        client_->cancel(cancel_request(*active_request_id_));
    } catch (...) {
        // Cancellation is best effort. The stop token still releases the local request waiter and
        // recovery will replace a Host that no longer accepts protocol input.
    }
}

void AssistantSession::recover() {
    {
        std::scoped_lock lock(active_mutex_);
        if (active_request_id_) {
            throw std::logic_error("cannot recover while a request is active");
        }
    }
    client_.reset();
    conversation_id_ = new_uuid();
    connect();
}

std::unique_ptr<AssistantSession> AssistantSession::from_environment() {
    const auto node = environment_value(L"WOWAI_NODE_BINARY");
    const auto host = environment_value(L"WOWAI_HOST_SCRIPT");
    const auto provider = environment_value(L"WOWAI_MODEL_PROVIDER");
    const auto model = environment_value(L"WOWAI_CLOUD_MODEL");
    const auto cloud_consent = environment_value(L"WOWAI_CLOUD_UPLOAD_CONSENT");
    const auto codex_binary = environment_value(L"WOWAI_CODEX_BINARY");
    const auto codex_lock = environment_value(L"WOWAI_CODEX_LOCK");
    const auto codex_root = environment_value(L"WOWAI_CODEX_ROOT");
    if (!node && !host && !provider && !model && !cloud_consent && !codex_binary && !codex_lock &&
        !codex_root) {
        return nullptr;
    }
    if (!node || !host || !provider || !model || !cloud_consent || !codex_binary || !codex_lock ||
        !codex_root || !cloud_provider(narrow_ascii(*provider)) || *cloud_consent != L"1") {
        throw std::runtime_error(
            "WOWAI_NODE_BINARY, WOWAI_HOST_SCRIPT, WOWAI_MODEL_PROVIDER, "
            "WOWAI_CLOUD_MODEL, WOWAI_CLOUD_UPLOAD_CONSENT=1, WOWAI_CODEX_BINARY, "
            "WOWAI_CODEX_LOCK, and WOWAI_CODEX_ROOT must explicitly configure a supported cloud "
            "path");
    }
    const auto provider_text = narrow_ascii(*provider);
    const auto credential = legacy_credential(provider_text);
    if (!credential || credential->empty()) {
        throw AssistantFailure(
            "AI_CREDENTIALS_MISSING",
            "缺少当前提供方 API key。产品设置优先使用 DPAPI；.env.local 仅作为开发回退。", false);
    }
    wowai::storage::AssistantSettings settings;
    settings.cloud_enabled = true;
    settings.cloud_provider = provider_text;
    settings.cloud_model = narrow_ascii(*model);
    settings.cloud_organization =
        narrow_ascii(environment_value(L"WOWAI_CLOUD_ORGANIZATION").value_or(L""));
    settings.cloud_region =
        narrow_ascii(environment_value(L"WOWAI_DASHSCOPE_REGION").value_or(L"singapore"));
    settings.cloud_resource =
        narrow_ascii(environment_value(provider_text == "dashscope" ? L"WOWAI_DASHSCOPE_WORKSPACE"
                                                                    : L"WOWAI_AZURE_RESOURCE")
                         .value_or(L""));
    settings.cloud_api_version =
        narrow_ascii(environment_value(L"WOWAI_AZURE_API_VERSION").value_or(L"v1"));
    auto options = configured_host_options(*node, *host, *credential, settings);
    return std::make_unique<AssistantSession>(options, provider_text, settings.cloud_model,
                                              configured_destination_host(settings));
}

std::unique_ptr<AssistantSession>
AssistantSession::from_secure_settings(const wowai::storage::AssistantSettings& settings,
                                       const wowai::storage::CredentialStore& credentials) {
    return from_connection_test(settings, credentials, std::nullopt);
}

std::unique_ptr<AssistantSession> AssistantSession::from_connection_test(
    const wowai::storage::AssistantSettings& settings,
    const wowai::storage::CredentialStore& credentials,
    const std::optional<std::string>& transient_credential) {
    if (!settings.cloud_enabled)
        return nullptr;
    if (!settings.valid())
        throw std::runtime_error("saved cloud configuration is invalid");
    const auto node = environment_value(L"WOWAI_NODE_BINARY");
    const auto host = environment_value(L"WOWAI_HOST_SCRIPT");
    const auto codex_binary = environment_value(L"WOWAI_CODEX_BINARY");
    const auto codex_lock = environment_value(L"WOWAI_CODEX_LOCK");
    const auto codex_root = environment_value(L"WOWAI_CODEX_ROOT");
    if (!node || !host || !codex_binary || !codex_lock || !codex_root) {
        throw std::runtime_error("locked Host/App Server runtime paths are incomplete");
    }
    auto secret = transient_credential;
    if (!secret || secret->empty()) {
        secret = credentials.read(settings.cloud_provider, settings.cloud_profile);
    }
    if (!secret) {
        throw AssistantFailure("AI_CREDENTIALS_MISSING", "当前提供方/配置档未保存 API key。",
                               false);
    }
    std::wstring credential(secret->begin(), secret->end());
    ::SecureZeroMemory(secret->data(), secret->size());
    auto options = configured_host_options(*node, *host, credential, settings);
    ::SecureZeroMemory(credential.data(), credential.size() * sizeof(wchar_t));
    return std::make_unique<AssistantSession>(options, settings.cloud_provider,
                                              settings.cloud_model,
                                              configured_destination_host(settings));
}

std::string AssistantSession::destination_host() const {
    if (!destination_host_.empty())
        return destination_host_;
    if (provider_ == "openai")
        return "api.openai.com";
    if (provider_ == "deepseek")
        return "api.deepseek.com";
    if (provider_ == "xai")
        return "api.x.ai";
    if (provider_ == "openrouter")
        return "openrouter.ai";
    return "unknown";
}

nlohmann::json
make_assistant_request(const std::string_view request_id, const std::string_view conversation_id,
                       const std::string_view created_at, const std::string_view question,
                       const std::string_view provider, const std::string_view model,
                       std::optional<ConfirmedImage> image, nlohmann::json observations,
                       nlohmann::json visual_bridge, const bool screen_observation_enabled) {
    nlohmann::json result{
        {"schemaVersion", protocol_version},
        {"requestId", request_id},
        {"conversationId", conversation_id},
        {"createdAt", created_at},
        {"mode", "general"},
        {"locale", "zh-CN"},
        {"gameFlavor", "retail"},
        {"question", question},
        {"character",
         {{"region", "cn"},
          {"realm", nullptr},
          {"name", nullptr},
          {"classId", nullptr},
          {"specializationId", nullptr},
          {"level", nullptr}}},
        {"images", nlohmann::json::array()},
        {"visualBridge", std::move(visual_bridge)},
        {"observations", std::move(observations)},
        {"privacy",
         {{"selectedWindowOnly", true},
          {"screenObservationEnabled", screen_observation_enabled},
          {"rawFramesPersisted", false}}},
        {"client",
         {{"addonVersion", nullptr}, {"companionVersion", "0.1.0-dev"}, {"uiScale", nullptr}}},
        {"runtime",
         {{"engine", "codex"},
          {"provider", provider},
          {"model", model},
          {"allowCloudUpload", cloud_provider(provider)}}}};
    if (image) {
        result["images"].push_back({{"id", image->id},
                                    {"mimeType", image->mime_type},
                                    {"captureScope", image->capture_scope},
                                    {"sha256", image->sha256},
                                    {"dataBase64", image->data_base64},
                                    {"privacyMaskApplied", image->privacy_mask_applied},
                                    {"userConfirmed", true},
                                    {"uploadDestination", provider},
                                    {"uploadPurpose", "visual-question"},
                                    {"uploadConfirmedAt", image->upload_confirmed_at},
                                    {"consentNoticeVersion", 1}});
    }
    const auto validation = validate_assistant_request(result);
    if (!validation) {
        throw std::invalid_argument(validation.message);
    }
    return result;
}

} // namespace wowai::codex
