#include "wowai/codex/protocol.hpp"

#include <algorithm>
#include <array>
#include <regex>
#include <unordered_set>

namespace wowai::codex {
namespace {

using Json = nlohmann::json;

ValidationResult accepted() { return {true, {}}; }
ValidationResult rejected(std::string message) { return {false, std::move(message)}; }

bool has_exact_keys(const Json& value, std::initializer_list<std::string_view> keys) {
    if (!value.is_object() || value.size() != keys.size()) {
        return false;
    }
    return std::ranges::all_of(keys, [&](std::string_view key) { return value.contains(key); });
}

bool is_string_between(const Json& value, std::size_t minimum, std::size_t maximum) {
    return value.is_string() && value.get_ref<const std::string&>().size() >= minimum &&
           value.get_ref<const std::string&>().size() <= maximum;
}

bool is_nullable_string(const Json& value, std::size_t maximum) {
    return value.is_null() || is_string_between(value, 0, maximum);
}

bool is_nonnegative_integer(const Json& value) {
    return value.is_number_unsigned() ||
           (value.is_number_integer() && value.get<std::int64_t>() >= 0);
}

bool is_positive_nullable_integer(const Json& value, std::uint64_t maximum) {
    return value.is_null() || (is_nonnegative_integer(value) && value.get<std::uint64_t>() >= 1 &&
                               value.get<std::uint64_t>() <= maximum);
}

bool is_one_of(const Json& value, std::initializer_list<std::string_view> choices) {
    if (!value.is_string()) {
        return false;
    }
    const auto& text = value.get_ref<const std::string&>();
    return std::ranges::any_of(choices, [&](std::string_view choice) { return text == choice; });
}

bool is_uuid(const Json& value) {
    static const std::regex pattern(
        R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$)");
    return value.is_string() && std::regex_match(value.get_ref<const std::string&>(), pattern);
}

bool is_base64(const Json& value) {
    static const std::regex pattern(R"(^[A-Za-z0-9+/]+={0,2}$)");
    return is_string_between(value, 12, 956000) &&
           std::regex_match(value.get_ref<const std::string&>(), pattern) &&
           value.get_ref<const std::string&>().size() % 4 == 0;
}

bool is_bridge_field(const Json& value) {
    return is_one_of(value,
                     {"class", "classId", "specialization", "specializationId", "level", "zone",
                      "mapId", "activity", "encounterId", "achievementId", "criteria", "event",
                      "skills", "talents", "actionSlots", "keyBindings"});
}

bool is_utc_timestamp(const Json& value) {
    static const std::regex pattern(
        R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]+)?Z$)");
    return value.is_string() && std::regex_match(value.get_ref<const std::string&>(), pattern);
}

bool is_valid_utf8(std::string_view text, std::size_t* code_points = nullptr) {
    std::size_t count = 0;
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index]);
        std::size_t length = 0;
        std::uint32_t value = 0;
        if (first <= 0x7f) {
            length = 1;
            value = first;
        } else if (first >= 0xc2 && first <= 0xdf) {
            length = 2;
            value = first & 0x1fU;
        } else if (first >= 0xe0 && first <= 0xef) {
            length = 3;
            value = first & 0x0fU;
        } else if (first >= 0xf0 && first <= 0xf4) {
            length = 4;
            value = first & 0x07U;
        } else {
            return false;
        }
        if (index + length > text.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xc0U) != 0x80U) {
                return false;
            }
            value = (value << 6U) | (continuation & 0x3fU);
        }
        if ((length == 3 && value < 0x800U) || (length == 4 && value < 0x10000U) ||
            (value >= 0xd800U && value <= 0xdfffU) || value > 0x10ffffU) {
            return false;
        }
        index += length;
        ++count;
    }
    if (code_points != nullptr) {
        *code_points = count;
    }
    return true;
}

bool is_error(const Json& value) {
    constexpr std::array codes{"WOW_WINDOW_NOT_FOUND",
                               "ANCHOR_NOT_FOUND",
                               "CAPTURE_DENIED",
                               "CAPTURE_EMPTY",
                               "PRIVACY_CONFIRM_REQUIRED",
                               "CODEX_NOT_INSTALLED",
                               "CODEX_VERSION_MISMATCH",
                               "CODEX_START_FAILED",
                               "CODEX_PROTOCOL_ERROR",
                               "CODEX_TOOL_BLOCKED",
                               "MODEL_PROVIDER_UNAVAILABLE",
                               "MODEL_CAPABILITY_MISSING",
                               "AI_CREDENTIALS_MISSING",
                               "AI_AUTH_FAILED",
                               "AI_MODEL_UNAVAILABLE",
                               "AI_NETWORK_UNAVAILABLE",
                               "AI_RATE_LIMITED",
                               "AI_TIMEOUT",
                               "AI_INVALID_RESPONSE",
                               "AI_USAGE_LIMIT_REACHED",
                               "AI_DUPLICATE_REQUEST_BLOCKED",
                               "KNOWLEDGE_STALE",
                               "POLICY_BLOCKED",
                               "BRIDGE_FRAME_INVALID",
                               "OBSERVATION_STALE",
                               "TEACHING_SESSION_INACTIVE"};
    if (!has_exact_keys(value, {"code", "message", "retryable"}) ||
        !is_string_between(value["message"], 1, 512) || !value["retryable"].is_boolean() ||
        !value["code"].is_string()) {
        return false;
    }
    const auto& code = value["code"].get_ref<const std::string&>();
    return std::ranges::find(codes, code) != codes.end();
}

bool is_answer(const Json& value) {
    if (!has_exact_keys(value,
                        {"summary", "nextSteps", "constraints", "uncertainties", "followUp"}) ||
        !is_string_between(value["summary"], 0, 8000) || !value["nextSteps"].is_array() ||
        value["nextSteps"].size() > 5 || !value["constraints"].is_array() ||
        value["constraints"].size() > 10 || !value["uncertainties"].is_array() ||
        value["uncertainties"].size() > 10 || !is_nullable_string(value["followUp"], 1000)) {
        return false;
    }
    const auto valid_list = [](const Json& list) {
        return std::ranges::all_of(
            list, [](const Json& item) { return is_string_between(item, 1, 1000); });
    };
    return valid_list(value["nextSteps"]) && valid_list(value["constraints"]) &&
           valid_list(value["uncertainties"]);
}

} // namespace

ValidationResult validate_assistant_request(const Json& value) {
    const bool has_bridge = value.is_object() && value.contains("visualBridge");
    if (!(has_bridge ? has_exact_keys(value, {"schemaVersion", "requestId", "conversationId",
                                              "createdAt", "mode", "locale", "gameFlavor",
                                              "question", "character", "images", "visualBridge",
                                              "observations", "privacy", "client", "runtime"})
                     : has_exact_keys(value, {"schemaVersion", "requestId", "conversationId",
                                              "createdAt", "mode", "locale", "gameFlavor",
                                              "question", "character", "images", "observations",
                                              "privacy", "client", "runtime"}))) {
        return rejected("request fields do not match contract");
    }
    std::size_t question_length = 0;
    if (value["schemaVersion"] != protocol_version || !is_uuid(value["requestId"]) ||
        !is_uuid(value["conversationId"]) || !is_utc_timestamp(value["createdAt"]) ||
        !is_one_of(value["mode"], {"achievement", "mount", "pet", "gear", "general", "coach"}) ||
        !is_string_between(value["locale"], 2, 35) || value["gameFlavor"] != "retail" ||
        !value["question"].is_string() ||
        !is_valid_utf8(value["question"].get_ref<const std::string&>(), &question_length) ||
        question_length < 1 || question_length > 4000) {
        return rejected("request scalar field is invalid");
    }

    const auto& character = value["character"];
    if (!has_exact_keys(character,
                        {"region", "realm", "name", "classId", "specializationId", "level"}) ||
        !is_one_of(character["region"], {"cn", "us", "eu", "kr", "tw"}) ||
        !is_nullable_string(character["realm"], 128) ||
        !is_nullable_string(character["name"], 128) ||
        !is_positive_nullable_integer(character["classId"], UINT32_MAX) ||
        !is_positive_nullable_integer(character["specializationId"], UINT32_MAX) ||
        !is_positive_nullable_integer(character["level"], 100)) {
        return rejected("character context is invalid");
    }

    if (!value["images"].is_array() || value["images"].size() > 1) {
        return rejected("image list is invalid");
    }
    for (const auto& image : value["images"]) {
        if (!has_exact_keys(image,
                            {"id", "mimeType", "captureScope", "sha256", "dataBase64",
                             "privacyMaskApplied", "userConfirmed", "uploadDestination",
                             "uploadPurpose", "uploadConfirmedAt", "consentNoticeVersion"}) ||
            !is_uuid(image["id"]) || image["mimeType"] != "image/png" ||
            !is_one_of(image["captureScope"], {"wow-window", "selected-region", "tooltip"}) ||
            !image["sha256"].is_string() ||
            image["sha256"].get_ref<const std::string&>().size() != 64 ||
            !is_base64(image["dataBase64"]) || !image["privacyMaskApplied"].is_boolean() ||
            image["userConfirmed"] != true ||
            !is_one_of(image["uploadDestination"],
                       {"openai", "deepseek", "xai", "openrouter", "dashscope", "azure-openai"}) ||
            image["uploadPurpose"] != "visual-question" ||
            !is_utc_timestamp(image["uploadConfirmedAt"]) || image["consentNoticeVersion"] != 1) {
            return rejected("image context is invalid");
        }
    }

    if (has_bridge && !value["visualBridge"].is_null()) {
        const auto& bridge = value["visualBridge"];
        if (!has_exact_keys(bridge, {"protocolVersion", "source", "sequence", "capturedAt",
                                     "confidence", "allowedFields", "unavailableFields"}) ||
            bridge["protocolVersion"] != 1 || bridge["source"] != "plugin-public" ||
            !is_nonnegative_integer(bridge["sequence"]) ||
            bridge["sequence"].get<std::uint64_t>() > UINT32_MAX ||
            !is_utc_timestamp(bridge["capturedAt"]) || !bridge["confidence"].is_number() ||
            bridge["confidence"].get<double>() < 0.0 || bridge["confidence"].get<double>() > 1.0 ||
            !bridge["allowedFields"].is_array() || bridge["allowedFields"].size() > 16 ||
            !bridge["unavailableFields"].is_array() || bridge["unavailableFields"].size() > 16 ||
            !std::ranges::all_of(bridge["allowedFields"], is_bridge_field) ||
            !std::ranges::all_of(bridge["unavailableFields"], is_bridge_field)) {
            return rejected("visual bridge context is invalid");
        }
    }

    if (!value["observations"].is_array() || value["observations"].size() > 32) {
        return rejected("observation list is invalid");
    }
    for (const auto& observation : value["observations"]) {
        if (!has_exact_keys(observation,
                            {"id", "source", "kind", "capturedAt", "confidence", "summary"}) ||
            !is_uuid(observation["id"]) ||
            !is_one_of(observation["source"],
                       {"plugin-public", "profile-cache", "screen-observed", "model-inferred"}) ||
            !is_one_of(observation["kind"], {"build", "game-state", "achievement-progress",
                                             "combat-ui", "screen-text"}) ||
            !is_utc_timestamp(observation["capturedAt"]) ||
            !observation["confidence"].is_number() ||
            observation["confidence"].get<double>() < 0.0 ||
            observation["confidence"].get<double>() > 1.0 ||
            !is_string_between(observation["summary"], 1, 4000)) {
            return rejected("observation is invalid");
        }
    }

    const auto& privacy = value["privacy"];
    if (!has_exact_keys(privacy,
                        {"selectedWindowOnly", "screenObservationEnabled", "rawFramesPersisted"}) ||
        privacy["selectedWindowOnly"] != true ||
        !privacy["screenObservationEnabled"].is_boolean() ||
        privacy["rawFramesPersisted"] != false) {
        return rejected("privacy boundary is invalid");
    }

    const auto& client = value["client"];
    if (!has_exact_keys(client, {"addonVersion", "companionVersion", "uiScale"}) ||
        !is_nullable_string(client["addonVersion"], 64) ||
        !is_string_between(client["companionVersion"], 1, 64) ||
        !(client["uiScale"].is_null() ||
          (client["uiScale"].is_number() && client["uiScale"].get<double>() >= 0.5 &&
           client["uiScale"].get<double>() <= 4.0))) {
        return rejected("client context is invalid");
    }

    const auto& runtime = value["runtime"];
    if (!has_exact_keys(runtime, {"engine", "provider", "model", "allowCloudUpload"}) ||
        runtime["engine"] != "codex" ||
        !is_one_of(runtime["provider"],
                   {"local-ollama", "local-lmstudio", "openai", "deepseek", "xai", "openrouter",
                    "dashscope", "azure-openai", "mock"}) ||
        !is_string_between(runtime["model"], 1, 128) || !runtime["allowCloudUpload"].is_boolean() ||
        (is_one_of(runtime["provider"],
                   {"openai", "deepseek", "xai", "openrouter", "dashscope", "azure-openai"}) &&
         runtime["allowCloudUpload"] != true) ||
        (!is_one_of(runtime["provider"],
                    {"openai", "deepseek", "xai", "openrouter", "dashscope", "azure-openai"}) &&
         runtime["allowCloudUpload"] != false) ||
        (!value["images"].empty() &&
         value["images"][0]["uploadDestination"] != runtime["provider"])) {
        return rejected("runtime context is invalid");
    }
    return accepted();
}

ValidationResult validate_assistant_response(const Json& value) {
    if (!has_exact_keys(value, {"schemaVersion", "requestId", "status", "mode", "answer", "sources",
                                "provenance", "usage", "error"}) ||
        value["schemaVersion"] != protocol_version || !is_uuid(value["requestId"]) ||
        !is_one_of(value["status"], {"completed", "needs_context", "refused", "failed"}) ||
        !is_one_of(value["mode"], {"achievement", "mount", "pet", "gear", "general", "coach"}) ||
        !is_answer(value["answer"]) || !value["sources"].is_array() ||
        value["sources"].size() > 20) {
        return rejected("response fields are invalid");
    }
    for (const auto& source : value["sources"]) {
        if (!has_exact_keys(source, {"title", "url", "dataVersion"}) ||
            !is_string_between(source["title"], 1, 256) ||
            !is_nullable_string(source["url"], 2048) ||
            !is_nullable_string(source["dataVersion"], 128) ||
            (source["url"].is_string() &&
             !source["url"].get_ref<const std::string&>().starts_with("https://"))) {
            return rejected("source is invalid");
        }
    }
    if (!value["provenance"].is_array() || value["provenance"].size() > 32) {
        return rejected("provenance is invalid");
    }
    for (const auto& provenance : value["provenance"]) {
        if (!has_exact_keys(provenance, {"observationId", "source", "confidence", "reason"}) ||
            !is_uuid(provenance["observationId"]) ||
            !is_one_of(provenance["source"],
                       {"plugin-public", "profile-cache", "screen-observed", "model-inferred"}) ||
            !provenance["confidence"].is_number() || provenance["confidence"].get<double>() < 0.0 ||
            provenance["confidence"].get<double>() > 1.0 ||
            !is_string_between(provenance["reason"], 1, 1000)) {
            return rejected("provenance item is invalid");
        }
    }
    const auto& usage = value["usage"];
    if (!has_exact_keys(usage, {"imageUsed", "knowledgeUsed", "screenObservationUsed",
                                "addonBridgeUsed", "runtime", "provider"}) ||
        !usage["imageUsed"].is_boolean() || !usage["knowledgeUsed"].is_boolean() ||
        !usage["screenObservationUsed"].is_boolean() || !usage["addonBridgeUsed"].is_boolean() ||
        usage["runtime"] != "codex" ||
        !is_one_of(usage["provider"], {"local-ollama", "local-lmstudio", "openai", "deepseek",
                                       "xai", "openrouter", "dashscope", "azure-openai", "mock"})) {
        return rejected("usage is invalid");
    }
    const bool failed = value["status"] == "failed";
    if ((failed && !is_error(value["error"])) || (!failed && !value["error"].is_null())) {
        return rejected("response error state is invalid");
    }
    return accepted();
}

ValidationResult validate_envelope(const Json& value) {
    if (!has_exact_keys(value, {"protocolVersion", "messageId", "kind", "requestId", "sequence",
                                "sentAt", "timeoutMs", "payload"}) ||
        value["protocolVersion"] != protocol_version || !is_uuid(value["messageId"]) ||
        !is_one_of(value["kind"], {"hello", "ready", "request", "cancel", "response", "error"}) ||
        !(value["requestId"].is_null() || is_uuid(value["requestId"])) ||
        !is_nonnegative_integer(value["sequence"]) || !is_utc_timestamp(value["sentAt"]) ||
        !(value["timeoutMs"].is_null() ||
          (is_nonnegative_integer(value["timeoutMs"]) &&
           value["timeoutMs"].get<std::uint64_t>() >= 1000 &&
           value["timeoutMs"].get<std::uint64_t>() <= max_timeout_ms))) {
        return rejected("envelope fields are invalid");
    }
    const auto& kind = value["kind"].get_ref<const std::string&>();
    const auto& payload = value["payload"];
    if (kind == "hello") {
        if (!value["requestId"].is_null() || value["sequence"] != 0 ||
            !value["timeoutMs"].is_null() ||
            !has_exact_keys(payload, {"supportedVersions", "maxMessageBytes"}) ||
            !payload["supportedVersions"].is_array() || payload["supportedVersions"].empty() ||
            !std::ranges::all_of(payload["supportedVersions"],
                                 [](const Json& item) { return item == protocol_version; }) ||
            !is_nonnegative_integer(payload["maxMessageBytes"]) ||
            payload["maxMessageBytes"].get<std::uint64_t>() < 1024 ||
            payload["maxMessageBytes"].get<std::uint64_t>() > max_message_bytes) {
            return rejected("hello envelope is invalid");
        }
    } else if (kind == "ready") {
        if (!value["requestId"].is_null() || value["sequence"] != 1 ||
            !value["timeoutMs"].is_null() ||
            !has_exact_keys(payload, {"selectedVersion", "maxMessageBytes"}) ||
            payload["selectedVersion"] != protocol_version ||
            !is_nonnegative_integer(payload["maxMessageBytes"]) ||
            payload["maxMessageBytes"].get<std::uint64_t>() < 1024 ||
            payload["maxMessageBytes"].get<std::uint64_t>() > max_message_bytes) {
            return rejected("ready envelope is invalid");
        }
    } else {
        if (value["requestId"].is_null()) {
            return rejected("request-scoped envelope lacks requestId");
        }
        if (kind == "request" &&
            (value["sequence"] != 0 || value["timeoutMs"].is_null() ||
             !validate_assistant_request(payload) || payload["requestId"] != value["requestId"])) {
            return rejected("request envelope is invalid");
        }
        if (kind == "response" &&
            (!value["timeoutMs"].is_null() || !validate_assistant_response(payload) ||
             payload["requestId"] != value["requestId"])) {
            return rejected("response envelope is invalid");
        }
        if (kind == "error" && (!value["timeoutMs"].is_null() || !is_error(payload))) {
            return rejected("error envelope is invalid");
        }
        if (kind == "cancel" &&
            (!value["timeoutMs"].is_null() || !has_exact_keys(payload, {"reason"}) ||
             !is_one_of(payload["reason"], {"user", "timeout", "shutdown"}))) {
            return rejected("cancel envelope is invalid");
        }
    }
    return accepted();
}

ValidationResult parse_json_line(std::string_view line, Json& value) {
    if (line.size() > max_message_bytes) {
        return rejected("message exceeds maximum byte length");
    }
    if (line.starts_with("\xef\xbb\xbf")) {
        return rejected("UTF-8 BOM is not permitted");
    }
    if (!is_valid_utf8(line) || line.find('\0') != std::string_view::npos ||
        line.find('\r') != std::string_view::npos || line.find('\n') != std::string_view::npos) {
        return rejected("message is not one valid UTF-8 JSON line");
    }
    try {
        value = Json::parse(line);
    } catch (const Json::parse_error&) {
        return rejected("message is not valid JSON");
    }
    return validate_envelope(value);
}

void from_json(const Json& value, AssistantRequest& request) {
    request.schema_version = value.at("schemaVersion").get<std::string>();
    request.request_id = value.at("requestId").get<std::string>();
    request.conversation_id = value.at("conversationId").get<std::string>();
    request.created_at = value.at("createdAt").get<std::string>();
    request.mode = value.at("mode").get<std::string>();
    request.locale = value.at("locale").get<std::string>();
    request.game_flavor = value.at("gameFlavor").get<std::string>();
    request.question = value.at("question").get<std::string>();
    const auto& character = value.at("character");
    request.character = {character.at("region").get<std::string>(),
                         character.at("realm").get<std::optional<std::string>>(),
                         character.at("name").get<std::optional<std::string>>(),
                         character.at("classId").get<std::optional<std::uint32_t>>(),
                         character.at("specializationId").get<std::optional<std::uint32_t>>(),
                         character.at("level").get<std::optional<std::uint32_t>>()};
    request.images.clear();
    for (const auto& image : value.at("images")) {
        request.images.push_back(
            {image.at("id").get<std::string>(), image.at("mimeType").get<std::string>(),
             image.at("captureScope").get<std::string>(), image.at("sha256").get<std::string>(),
             image.at("dataBase64").get<std::string>(), image.at("privacyMaskApplied").get<bool>(),
             image.at("userConfirmed").get<bool>()});
    }
    request.observations = value.at("observations");
    request.privacy = value.at("privacy");
    request.client = value.at("client");
    request.runtime = value.at("runtime");
}

void from_json(const Json& value, AssistantError& error) {
    value.at("code").get_to(error.code);
    value.at("message").get_to(error.message);
    value.at("retryable").get_to(error.retryable);
}

void from_json(const Json& value, AssistantResponse& response) {
    value.at("schemaVersion").get_to(response.schema_version);
    value.at("requestId").get_to(response.request_id);
    value.at("status").get_to(response.status);
    value.at("mode").get_to(response.mode);
    response.answer = value.at("answer");
    response.sources = value.at("sources");
    response.provenance = value.at("provenance");
    response.usage = value.at("usage");
    response.error = value.at("error").get<std::optional<AssistantError>>();
}

void from_json(const Json& value, ProtocolEnvelope& envelope) {
    value.at("protocolVersion").get_to(envelope.protocol_version);
    value.at("messageId").get_to(envelope.message_id);
    value.at("kind").get_to(envelope.kind);
    envelope.request_id = value.at("requestId").get<std::optional<std::string>>();
    value.at("sequence").get_to(envelope.sequence);
    value.at("sentAt").get_to(envelope.sent_at);
    envelope.timeout_ms = value.at("timeoutMs").get<std::optional<std::uint32_t>>();
    envelope.payload = value.at("payload");
}

ValidationResult ProtocolSequenceTracker::accept(const ProtocolEnvelope& envelope) {
    if (envelope.kind == "hello") {
        if (negotiation_state_ != NegotiationState::new_session) {
            return rejected("duplicate or late hello");
        }
        negotiation_state_ = NegotiationState::hello_received;
        return accepted();
    }
    if (envelope.kind == "ready") {
        if (negotiation_state_ != NegotiationState::hello_received) {
            return rejected("ready before hello");
        }
        negotiation_state_ = NegotiationState::ready;
        return accepted();
    }
    if (negotiation_state_ != NegotiationState::ready || !envelope.request_id) {
        return rejected("request before negotiation");
    }
    if (envelope.kind == "request") {
        if (next_by_request_.contains(*envelope.request_id)) {
            return rejected("duplicate requestId");
        }
        next_by_request_[*envelope.request_id] = 1;
        return accepted();
    }
    const auto current = next_by_request_.find(*envelope.request_id);
    if (current == next_by_request_.end() || current->second != envelope.sequence) {
        return rejected("out-of-order message");
    }
    if (envelope.kind == "cancel" || envelope.kind == "response" || envelope.kind == "error") {
        next_by_request_.erase(current);
    } else {
        ++current->second;
    }
    return accepted();
}

} // namespace wowai::codex
