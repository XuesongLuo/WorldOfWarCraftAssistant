#pragma once

#include "wowai/storage/settings.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

struct sqlite3;

namespace wowai::storage {

class LocalDatabase final {
  public:
    explicit LocalDatabase(std::filesystem::path path);
    ~LocalDatabase();

    LocalDatabase(const LocalDatabase&) = delete;
    LocalDatabase& operator=(const LocalDatabase&) = delete;

    [[nodiscard]] AssistantSettings load_settings();
    void save_settings(const AssistantSettings& settings);
    void restore_default_settings();

    void save_exchange(std::string_view question, std::string_view answer);
    [[nodiscard]] std::int64_t saved_exchange_count() const;
    void clear_conversations();
    void reset_all();

    [[nodiscard]] int schema_version() const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

  private:
    void open_and_migrate();
    void recover_corrupt_database();
    void close() noexcept;
    void execute(std::string_view sql) const;

    std::filesystem::path path_;
    sqlite3* database_{};
};

} // namespace wowai::storage
