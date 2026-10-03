#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace spdlog {
class logger;
}

namespace wowai::storage {

[[nodiscard]] std::string
redact_diagnostic(std::string_view message,
                  const std::vector<std::string_view>& sensitive_values = {});

class SafeLog final {
  public:
    explicit SafeLog(const std::filesystem::path& directory);
    ~SafeLog();

    SafeLog(const SafeLog&) = delete;
    SafeLog& operator=(const SafeLog&) = delete;

    void info(std::string_view event_code, std::string_view diagnostic = {});
    void error(std::string_view event_code, std::string_view diagnostic = {});
    void flush() noexcept;

  private:
    std::shared_ptr<spdlog::logger> logger_;
};

} // namespace wowai::storage
