#include "wowai/storage/local_database.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

namespace wowai::storage {
namespace {

constexpr int current_schema_version = 3;

class DatabaseCorrupt final : public std::runtime_error {
  public:
    DatabaseCorrupt() : std::runtime_error("local database is corrupt") {}
};

class Statement final {
  public:
    Statement(sqlite3* database, const char* sql) {
        const int result = ::sqlite3_prepare_v2(database, sql, -1, &value_, nullptr);
        if (result == SQLITE_CORRUPT || result == SQLITE_NOTADB) {
            throw DatabaseCorrupt{};
        }
        if (result != SQLITE_OK) {
            throw std::runtime_error("could not prepare local database statement");
        }
    }
    ~Statement() { ::sqlite3_finalize(value_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    [[nodiscard]] sqlite3_stmt* get() const noexcept { return value_; }

  private:
    sqlite3_stmt* value_{};
};

[[nodiscard]] std::int64_t read_integer(sqlite3* database, const char* key,
                                        const std::int64_t fallback) {
    Statement statement{database, "SELECT value FROM settings WHERE key = ?1"};
    ::sqlite3_bind_text(statement.get(), 1, key, -1, SQLITE_STATIC);
    if (::sqlite3_step(statement.get()) != SQLITE_ROW) {
        return fallback;
    }
    const auto* text = reinterpret_cast<const char*>(::sqlite3_column_text(statement.get(), 0));
    if (text == nullptr) {
        return fallback;
    }
    try {
        std::size_t parsed{};
        const std::string value{text};
        const auto result = std::stoll(value, &parsed);
        return parsed == value.size() ? result : fallback;
    } catch (...) {
        return fallback;
    }
}

void write_integer(sqlite3* database, const char* key, const std::int64_t value) {
    Statement statement{database, "INSERT INTO settings(key,value) VALUES(?1,?2) "
                                  "ON CONFLICT(key) DO UPDATE SET value=excluded.value"};
    const std::string text = std::to_string(value);
    ::sqlite3_bind_text(statement.get(), 1, key, -1, SQLITE_STATIC);
    ::sqlite3_bind_text(statement.get(), 2, text.c_str(), -1, SQLITE_TRANSIENT);
    if (::sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error("could not save local setting");
    }
}

[[nodiscard]] std::string read_text(sqlite3* database, const char* key,
                                    const std::string_view fallback) {
    Statement statement{database, "SELECT value FROM settings WHERE key = ?1"};
    ::sqlite3_bind_text(statement.get(), 1, key, -1, SQLITE_STATIC);
    if (::sqlite3_step(statement.get()) != SQLITE_ROW)
        return std::string{fallback};
    const auto* text = reinterpret_cast<const char*>(::sqlite3_column_text(statement.get(), 0));
    return text == nullptr ? std::string{fallback} : std::string{text};
}

void write_text(sqlite3* database, const char* key, const std::string_view value) {
    Statement statement{database, "INSERT INTO settings(key,value) VALUES(?1,?2) "
                                  "ON CONFLICT(key) DO UPDATE SET value=excluded.value"};
    ::sqlite3_bind_text(statement.get(), 1, key, -1, SQLITE_STATIC);
    ::sqlite3_bind_text64(statement.get(), 2, value.data(), value.size(), SQLITE_TRANSIENT,
                          SQLITE_UTF8);
    if (::sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error("could not save local setting");
    }
}

[[nodiscard]] std::int64_t scalar_count(sqlite3* database, const char* sql) {
    Statement statement{database, sql};
    if (::sqlite3_step(statement.get()) != SQLITE_ROW) {
        throw std::runtime_error("could not count cloud request audit records");
    }
    return ::sqlite3_column_int64(statement.get(), 0);
}

[[nodiscard]] std::uint32_t stopped_limit(const std::uint32_t configured,
                                          const std::uint32_t percent) {
    return std::max<std::uint32_t>(1, (configured * percent) / 100);
}

} // namespace

LocalDatabase::LocalDatabase(std::filesystem::path path) : path_(std::move(path)) {
    if (path_.empty()) {
        throw std::invalid_argument("local database path must not be empty");
    }
    if (const auto parent = path_.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    try {
        open_and_migrate();
    } catch (const DatabaseCorrupt&) {
        close();
        recover_corrupt_database();
        open_and_migrate();
    } catch (...) {
        close();
        throw;
    }
}

LocalDatabase::~LocalDatabase() { close(); }

void LocalDatabase::open_and_migrate() {
    const auto path_text = path_.u8string();
    if (::sqlite3_open_v2(reinterpret_cast<const char*>(path_text.c_str()), &database_,
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                          nullptr) != SQLITE_OK) {
        close();
        throw std::runtime_error("could not open local database");
    }
    execute("PRAGMA foreign_keys=ON");
    execute("PRAGMA secure_delete=ON");
    execute("PRAGMA journal_mode=WAL");
    const int starting_version = schema_version();
    if (starting_version > current_schema_version) {
        throw std::runtime_error("local database was created by a newer application version");
    }
    execute("BEGIN IMMEDIATE");
    try {
        if (starting_version < 1) {
            execute("CREATE TABLE settings("
                    "key TEXT PRIMARY KEY NOT NULL,value TEXT NOT NULL) WITHOUT ROWID");
            execute("PRAGMA user_version=1");
        }
        if (starting_version < 2) {
            execute("CREATE TABLE conversation_exchanges("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,created_at TEXT NOT NULL DEFAULT "
                    "CURRENT_TIMESTAMP,question TEXT NOT NULL,answer TEXT NOT NULL)");
            execute("PRAGMA user_version=2");
        }
        if (starting_version < 3) {
            execute("CREATE TABLE cloud_request_audit("
                    "request_id TEXT PRIMARY KEY NOT NULL,created_at TEXT NOT NULL DEFAULT "
                    "CURRENT_TIMESTAMP,provider TEXT NOT NULL,profile TEXT NOT NULL,"
                    "model TEXT NOT NULL,destination TEXT NOT NULL,includes_image INTEGER NOT "
                    "NULL CHECK(includes_image IN (0,1))) WITHOUT ROWID");
            execute("CREATE INDEX cloud_request_audit_created_at ON "
                    "cloud_request_audit(created_at)");
            execute("PRAGMA user_version=3");
        }
        execute("COMMIT");
    } catch (...) {
        try {
            execute("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
    if (schema_version() != current_schema_version) {
        throw std::runtime_error("unsupported local database schema");
    }
}

void LocalDatabase::recover_corrupt_database() {
    if (!std::filesystem::exists(path_)) {
        return;
    }
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    const auto backup = path_.wstring() + L".corrupt-" + std::to_wstring(stamp);
    std::filesystem::rename(path_, backup);
    std::error_code ignored;
    std::filesystem::remove(path_.wstring() + L"-wal", ignored);
    std::filesystem::remove(path_.wstring() + L"-shm", ignored);
}

void LocalDatabase::close() noexcept {
    if (database_ != nullptr) {
        ::sqlite3_close_v2(database_);
        database_ = nullptr;
    }
}

void LocalDatabase::execute(const std::string_view sql) const {
    char* message{};
    const int result =
        ::sqlite3_exec(database_, std::string{sql}.c_str(), nullptr, nullptr, &message);
    if (result != SQLITE_OK) {
        ::sqlite3_free(message);
        if (result == SQLITE_CORRUPT || result == SQLITE_NOTADB) {
            throw DatabaseCorrupt{};
        }
        throw std::runtime_error("local database operation failed");
    }
}

AssistantSettings LocalDatabase::load_settings() {
    auto result = AssistantSettings::defaults();
    result.hotkey_enabled = read_integer(database_, "hotkey.enabled", result.hotkey_enabled) != 0;
    result.hotkey_modifiers = static_cast<std::uint32_t>(
        read_integer(database_, "hotkey.modifiers", result.hotkey_modifiers));
    result.hotkey_virtual_key = static_cast<std::uint32_t>(
        read_integer(database_, "hotkey.virtual_key", result.hotkey_virtual_key));
    result.overlay_opacity_percent = static_cast<std::uint32_t>(
        read_integer(database_, "overlay.opacity", result.overlay_opacity_percent));
    result.overlay_font_size_px = static_cast<std::uint32_t>(
        read_integer(database_, "overlay.font_size", result.overlay_font_size_px));
    result.save_conversation_history =
        read_integer(database_, "privacy.save_history", result.save_conversation_history) != 0;
    result.cloud_enabled = read_integer(database_, "cloud.enabled", result.cloud_enabled) != 0;
    result.cloud_provider = read_text(database_, "cloud.provider", result.cloud_provider);
    result.cloud_model = read_text(database_, "cloud.model", result.cloud_model);
    result.cloud_profile = read_text(database_, "cloud.profile", result.cloud_profile);
    result.cloud_organization =
        read_text(database_, "cloud.organization", result.cloud_organization);
    result.cloud_region = read_text(database_, "cloud.region", result.cloud_region);
    result.cloud_resource = read_text(database_, "cloud.resource", result.cloud_resource);
    result.cloud_api_version = read_text(database_, "cloud.api_version", result.cloud_api_version);
    result.cloud_session_request_limit = static_cast<std::uint32_t>(
        read_integer(database_, "cloud.limit.session", result.cloud_session_request_limit));
    result.cloud_daily_request_limit = static_cast<std::uint32_t>(
        read_integer(database_, "cloud.limit.day", result.cloud_daily_request_limit));
    result.cloud_monthly_request_limit = static_cast<std::uint32_t>(
        read_integer(database_, "cloud.limit.month", result.cloud_monthly_request_limit));
    result.cloud_stop_threshold_percent = static_cast<std::uint32_t>(
        read_integer(database_, "cloud.limit.stop_percent", result.cloud_stop_threshold_percent));
    if (!result.valid()) {
        restore_default_settings();
        return AssistantSettings::defaults();
    }
    return result;
}

void LocalDatabase::save_settings(const AssistantSettings& settings) {
    if (!settings.valid()) {
        throw std::invalid_argument("assistant settings are invalid");
    }
    execute("BEGIN IMMEDIATE");
    try {
        write_integer(database_, "hotkey.enabled", settings.hotkey_enabled);
        write_integer(database_, "hotkey.modifiers", settings.hotkey_modifiers);
        write_integer(database_, "hotkey.virtual_key", settings.hotkey_virtual_key);
        write_integer(database_, "overlay.opacity", settings.overlay_opacity_percent);
        write_integer(database_, "overlay.font_size", settings.overlay_font_size_px);
        write_integer(database_, "privacy.save_history", settings.save_conversation_history);
        write_integer(database_, "cloud.enabled", settings.cloud_enabled);
        write_text(database_, "cloud.provider", settings.cloud_provider);
        write_text(database_, "cloud.model", settings.cloud_model);
        write_text(database_, "cloud.profile", settings.cloud_profile);
        write_text(database_, "cloud.organization", settings.cloud_organization);
        write_text(database_, "cloud.region", settings.cloud_region);
        write_text(database_, "cloud.resource", settings.cloud_resource);
        write_text(database_, "cloud.api_version", settings.cloud_api_version);
        write_integer(database_, "cloud.limit.session", settings.cloud_session_request_limit);
        write_integer(database_, "cloud.limit.day", settings.cloud_daily_request_limit);
        write_integer(database_, "cloud.limit.month", settings.cloud_monthly_request_limit);
        write_integer(database_, "cloud.limit.stop_percent", settings.cloud_stop_threshold_percent);
        if (!settings.save_conversation_history) {
            execute("DELETE FROM conversation_exchanges");
        }
        execute("COMMIT");
    } catch (...) {
        execute("ROLLBACK");
        throw;
    }
    if (!settings.save_conversation_history) {
        execute("PRAGMA wal_checkpoint(TRUNCATE)");
    }
}

void LocalDatabase::restore_default_settings() {
    execute("DELETE FROM settings");
    save_settings(AssistantSettings::defaults());
    execute("VACUUM");
    execute("PRAGMA wal_checkpoint(TRUNCATE)");
}

void LocalDatabase::save_exchange(const std::string_view question, const std::string_view answer) {
    if (!load_settings().save_conversation_history) {
        return;
    }
    Statement statement{database_,
                        "INSERT INTO conversation_exchanges(question,answer) VALUES(?1,?2)"};
    ::sqlite3_bind_text64(statement.get(), 1, question.data(), question.size(), SQLITE_TRANSIENT,
                          SQLITE_UTF8);
    ::sqlite3_bind_text64(statement.get(), 2, answer.data(), answer.size(), SQLITE_TRANSIENT,
                          SQLITE_UTF8);
    if (::sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error("could not save conversation exchange");
    }
    execute("DELETE FROM conversation_exchanges WHERE id NOT IN ("
            "SELECT id FROM conversation_exchanges ORDER BY id DESC LIMIT 100)");
}

std::int64_t LocalDatabase::saved_exchange_count() const {
    Statement statement{database_, "SELECT COUNT(*) FROM conversation_exchanges"};
    if (::sqlite3_step(statement.get()) != SQLITE_ROW) {
        throw std::runtime_error("could not count conversation exchanges");
    }
    return ::sqlite3_column_int64(statement.get(), 0);
}

void LocalDatabase::clear_conversations() { execute("DELETE FROM conversation_exchanges"); }

CloudRequestAuthorization LocalDatabase::authorize_cloud_request(
    const AssistantSettings& settings, const std::uint32_t session_requests,
    const std::string_view request_id, const std::string_view destination,
    const bool includes_image) {
    if (!settings.cloud_enabled)
        return CloudRequestAuthorization::cloud_disabled;
    if (!settings.valid() || request_id.empty() || destination.empty()) {
        throw std::invalid_argument("cloud request audit metadata is invalid");
    }
    Statement duplicate{database_, "SELECT COUNT(*) FROM cloud_request_audit WHERE request_id=?1"};
    ::sqlite3_bind_text64(duplicate.get(), 1, request_id.data(), request_id.size(),
                          SQLITE_TRANSIENT, SQLITE_UTF8);
    if (::sqlite3_step(duplicate.get()) != SQLITE_ROW) {
        throw std::runtime_error("could not check cloud request idempotency");
    }
    if (::sqlite3_column_int64(duplicate.get(), 0) != 0)
        return CloudRequestAuthorization::duplicate;
    if (session_requests >= stopped_limit(settings.cloud_session_request_limit,
                                          settings.cloud_stop_threshold_percent)) {
        return CloudRequestAuthorization::session_limit;
    }
    const auto daily = scalar_count(database_, "SELECT COUNT(*) FROM cloud_request_audit WHERE "
                                               "created_at >= datetime('now','start of day')");
    if (daily >=
        stopped_limit(settings.cloud_daily_request_limit, settings.cloud_stop_threshold_percent)) {
        return CloudRequestAuthorization::daily_limit;
    }
    const auto monthly = scalar_count(database_, "SELECT COUNT(*) FROM cloud_request_audit WHERE "
                                                 "created_at >= datetime('now','start of month')");
    if (monthly >= stopped_limit(settings.cloud_monthly_request_limit,
                                 settings.cloud_stop_threshold_percent)) {
        return CloudRequestAuthorization::monthly_limit;
    }
    Statement insert{database_, "INSERT INTO cloud_request_audit("
                                "request_id,provider,profile,model,destination,includes_image) "
                                "VALUES(?1,?2,?3,?4,?5,?6)"};
    const std::array<std::string_view, 5> values{request_id, settings.cloud_provider,
                                                 settings.cloud_profile, settings.cloud_model,
                                                 destination};
    for (std::size_t index = 0; index < values.size(); ++index) {
        ::sqlite3_bind_text64(insert.get(), static_cast<int>(index + 1), values[index].data(),
                              values[index].size(), SQLITE_TRANSIENT, SQLITE_UTF8);
    }
    ::sqlite3_bind_int(insert.get(), 6, includes_image ? 1 : 0);
    if (::sqlite3_step(insert.get()) != SQLITE_DONE) {
        if (::sqlite3_extended_errcode(database_) == SQLITE_CONSTRAINT_PRIMARYKEY) {
            return CloudRequestAuthorization::duplicate;
        }
        throw std::runtime_error("could not record cloud request audit metadata");
    }
    return CloudRequestAuthorization::allowed;
}

std::int64_t LocalDatabase::cloud_audit_count() const {
    return scalar_count(database_, "SELECT COUNT(*) FROM cloud_request_audit");
}

void LocalDatabase::reset_all() {
    execute("BEGIN IMMEDIATE");
    try {
        execute("DELETE FROM conversation_exchanges");
        execute("DELETE FROM cloud_request_audit");
        execute("DELETE FROM settings");
        execute("COMMIT");
    } catch (...) {
        execute("ROLLBACK");
        throw;
    }
    save_settings(AssistantSettings::defaults());
    execute("VACUUM");
    execute("PRAGMA wal_checkpoint(TRUNCATE)");
}

int LocalDatabase::schema_version() const {
    Statement statement{database_, "PRAGMA user_version"};
    return ::sqlite3_step(statement.get()) == SQLITE_ROW ? ::sqlite3_column_int(statement.get(), 0)
                                                         : 0;
}

const std::filesystem::path& LocalDatabase::path() const noexcept { return path_; }

} // namespace wowai::storage
