#include "wowai/storage/safe_log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <regex>
#include <stdexcept>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

namespace wowai::storage {

std::string redact_diagnostic(const std::string_view message,
                              const std::vector<std::string_view>& sensitive_values) {
    std::string result{message.substr(0, 4096)};
    for (const auto value : sensitive_values) {
        if (value.size() < 4) {
            continue;
        }
        std::size_t position{};
        while ((position = result.find(value, position)) != std::string::npos) {
            result.replace(position, value.size(), "[REDACTED]");
            position += 10;
        }
    }
    static const std::array patterns{
        std::regex{R"((Authorization\s*:\s*(?:Bearer\s+)?)[^\s,;]+)", std::regex::icase},
        std::regex{R"(((?:api[_ -]?key|token|secret|password)\s*[=:]\s*)[^\s,;]+)",
                   std::regex::icase},
        std::regex{R"(\b(?:sk|key)-[A-Za-z0-9_-]{8,}\b)", std::regex::icase},
        std::regex{R"(data:image\/[A-Za-z0-9.+-]+;base64,[A-Za-z0-9+\/=]{16,})",
                   std::regex::icase}};
    result = std::regex_replace(result, patterns[0], "$1[REDACTED]");
    result = std::regex_replace(result, patterns[1], "$1[REDACTED]");
    result = std::regex_replace(result, patterns[2], "[REDACTED]");
    result = std::regex_replace(result, patterns[3], "[REDACTED_IMAGE]");
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    std::replace(result.begin(), result.end(), '\n', ' ');
    return result;
}

SafeLog::SafeLog(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    logger_ = spdlog::rotating_logger_mt(
        "wowai-safe-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)),
        (directory / L"companion.log").string(), 1024 * 1024, 3, true);
    logger_->set_pattern("%Y-%m-%dT%H:%M:%S.%eZ [%l] %v");
    logger_->flush_on(spdlog::level::warn);
}

SafeLog::~SafeLog() {
    if (logger_) {
        const auto name = logger_->name();
        logger_->flush();
        logger_.reset();
        spdlog::drop(name);
    }
}

void SafeLog::info(const std::string_view event_code, const std::string_view diagnostic) {
    logger_->info("event={} detail={}", redact_diagnostic(event_code),
                  redact_diagnostic(diagnostic));
}

void SafeLog::error(const std::string_view event_code, const std::string_view diagnostic) {
    logger_->error("event={} detail={}", redact_diagnostic(event_code),
                   redact_diagnostic(diagnostic));
}

void SafeLog::flush() noexcept {
    if (logger_) {
        logger_->flush();
    }
}

} // namespace wowai::storage
