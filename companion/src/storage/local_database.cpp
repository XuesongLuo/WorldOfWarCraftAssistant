#include "wowai/storage/local_database.hpp"

#include <array>
#include <chrono>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

namespace wowai::storage {
namespace {

constexpr int current_schema_version = 2;

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

void LocalDatabase::reset_all() {
    execute("BEGIN IMMEDIATE");
    try {
        execute("DELETE FROM conversation_exchanges");
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
