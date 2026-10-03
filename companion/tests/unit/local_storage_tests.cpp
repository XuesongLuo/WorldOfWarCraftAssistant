#include "wowai/codex/assistant_session.hpp"
#include "wowai/storage/application_paths.hpp"
#include "wowai/storage/credential_store.hpp"
#include "wowai/storage/local_data.hpp"
#include "wowai/storage/local_database.hpp"
#include "wowai/storage/safe_log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <sqlite3.h>

namespace {

class TestDirectory final {
  public:
    TestDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                (L"wowai-storage-test-" +
                 std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TestDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

std::string read_binary(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("local database migrates first install and persists validated settings") {
    TestDirectory directory;
    const auto database_path = directory.path() / L"assistant.db";
    {
        wowai::storage::LocalDatabase database{database_path};
        CHECK(database.schema_version() == 3);
        CHECK(database.load_settings() == wowai::storage::AssistantSettings::defaults());

        auto settings = wowai::storage::AssistantSettings::defaults();
        settings.hotkey_virtual_key = 'G';
        settings.overlay_opacity_percent = 80;
        settings.overlay_font_size_px = 19;
        settings.save_conversation_history = true;
        database.save_settings(settings);
        database.save_exchange("question", "answer");
        CHECK(database.saved_exchange_count() == 1);
    }
    wowai::storage::LocalDatabase reopened{database_path};
    CHECK(reopened.load_settings().hotkey_virtual_key == 'G');
    CHECK(reopened.saved_exchange_count() == 1);
}

TEST_CASE("local database supports a non-persistent safe fallback") {
    wowai::storage::LocalDatabase database{std::filesystem::path{L":memory:"}};
    CHECK(database.schema_version() == 3);
    auto settings = database.load_settings();
    settings.overlay_opacity_percent = 75;
    database.save_settings(settings);
    CHECK(database.load_settings().overlay_opacity_percent == 75);
}

TEST_CASE("disabled conversation storage writes nothing and clears prior history") {
    TestDirectory directory;
    wowai::storage::LocalDatabase database{directory.path() / L"assistant.db"};
    auto settings = database.load_settings();
    database.save_exchange("not", "stored");
    CHECK(database.saved_exchange_count() == 0);
    settings.save_conversation_history = true;
    database.save_settings(settings);
    database.save_exchange("stored", "temporarily");
    CHECK(database.saved_exchange_count() == 1);
    settings.save_conversation_history = false;
    database.save_settings(settings);
    CHECK(database.saved_exchange_count() == 0);
}

TEST_CASE("reset removes conversation plaintext from SQLite and WAL files") {
    TestDirectory directory;
    const auto path = directory.path() / L"assistant.db";
    {
        wowai::storage::LocalDatabase database{path};
        auto settings = database.load_settings();
        settings.save_conversation_history = true;
        database.save_settings(settings);
        database.save_exchange("unique-sensitive-question-91827", "unique-sensitive-answer-39281");
        database.reset_all();
        CHECK(database.saved_exchange_count() == 0);
    }
    CHECK(read_binary(path).find("unique-sensitive-question-91827") == std::string::npos);
    const auto wal = path.wstring() + L"-wal";
    if (std::filesystem::exists(wal)) {
        CHECK(read_binary(wal).find("unique-sensitive-question-91827") == std::string::npos);
    }
}

TEST_CASE("enabled conversation history is bounded") {
    TestDirectory directory;
    wowai::storage::LocalDatabase database{directory.path() / L"assistant.db"};
    auto settings = database.load_settings();
    settings.save_conversation_history = true;
    database.save_settings(settings);
    for (int index = 0; index < 120; ++index) {
        database.save_exchange("question-" + std::to_string(index),
                               "answer-" + std::to_string(index));
    }
    CHECK(database.saved_exchange_count() == 100);
}

TEST_CASE("corrupt local database is archived and defaults recover") {
    TestDirectory directory;
    const auto path = directory.path() / L"assistant.db";
    {
        std::ofstream output{path, std::ios::binary};
        output << "not-a-sqlite-database";
    }
    wowai::storage::LocalDatabase database{path};
    CHECK(database.schema_version() == 3);
    CHECK(database.load_settings() == wowai::storage::AssistantSettings::defaults());
    std::size_t backups{};
    for (const auto& entry : std::filesystem::directory_iterator(directory.path())) {
        if (entry.path().filename().wstring().find(L"assistant.db.corrupt-") == 0)
            ++backups;
    }
    CHECK(backups == 1);
}

TEST_CASE("local database upgrades v1 atomically and refuses a newer schema") {
    TestDirectory directory;
    const auto path = directory.path() / L"assistant.db";
    sqlite3* raw{};
    const auto path_text = path.u8string();
    REQUIRE(::sqlite3_open(reinterpret_cast<const char*>(path_text.c_str()), &raw) == SQLITE_OK);
    REQUIRE(::sqlite3_exec(raw,
                           "CREATE TABLE settings(key TEXT PRIMARY KEY NOT NULL,value TEXT NOT "
                           "NULL) WITHOUT ROWID; PRAGMA user_version=1;",
                           nullptr, nullptr, nullptr) == SQLITE_OK);
    ::sqlite3_close(raw);
    {
        wowai::storage::LocalDatabase upgraded{path};
        CHECK(upgraded.schema_version() == 3);
        upgraded.clear_conversations();
    }
    REQUIRE(::sqlite3_open(reinterpret_cast<const char*>(path_text.c_str()), &raw) == SQLITE_OK);
    REQUIRE(::sqlite3_exec(raw, "PRAGMA user_version=99", nullptr, nullptr, nullptr) == SQLITE_OK);
    ::sqlite3_close(raw);
    CHECK_THROWS_AS(wowai::storage::LocalDatabase{path}, std::runtime_error);
    CHECK(std::filesystem::exists(path));
}

TEST_CASE("invalid persisted settings recover to safe defaults") {
    TestDirectory directory;
    const auto path = directory.path() / L"assistant.db";
    {
        wowai::storage::LocalDatabase database{path};
    }
    sqlite3* raw{};
    const auto path_text = path.u8string();
    REQUIRE(::sqlite3_open(reinterpret_cast<const char*>(path_text.c_str()), &raw) == SQLITE_OK);
    REQUIRE(::sqlite3_exec(raw,
                           "INSERT OR REPLACE INTO settings(key,value) "
                           "VALUES('overlay.opacity','999')",
                           nullptr, nullptr, nullptr) == SQLITE_OK);
    ::sqlite3_close(raw);
    wowai::storage::LocalDatabase database{path};
    CHECK(database.load_settings() == wowai::storage::AssistantSettings::defaults());
}

TEST_CASE("credential store uses DPAPI ciphertext and supports revocation") {
    TestDirectory directory;
    wowai::storage::CredentialStore credentials{directory.path() / L"Credentials"};
    constexpr std::string_view secret = "sk-never-plain-text-123456";
    credentials.write("openai", secret);
    const auto file = directory.path() / L"Credentials" / L"openai--default.dpapi";
    CHECK(std::filesystem::exists(file));
    CHECK(read_binary(file).find(secret) == std::string::npos);
    REQUIRE(credentials.read("openai"));
    CHECK(*credentials.read("openai") == secret);
    credentials.erase("openai");
    CHECK_FALSE(credentials.read("openai"));
    CHECK_FALSE(wowai::storage::CredentialStore::valid_provider("../escape"));
}

TEST_CASE("credentials are isolated by provider and profile entropy") {
    TestDirectory directory;
    wowai::storage::CredentialStore credentials{directory.path() / L"Credentials"};
    credentials.write("openai", "personal", "secret-one");
    credentials.write("openai", "guild", "secret-two");
    credentials.write("xai", "personal", "secret-three");
    CHECK(credentials.read("openai", "personal") == "secret-one");
    CHECK(credentials.read("openai", "guild") == "secret-two");
    CHECK(credentials.read("xai", "personal") == "secret-three");
    credentials.write("openai", "guild", "replacement-secret");
    CHECK(credentials.read("openai", "guild") == "replacement-secret");
    credentials.erase("openai", "personal");
    CHECK_FALSE(credentials.read("openai", "personal"));
    CHECK(credentials.read("openai", "guild") == "replacement-secret");
}

TEST_CASE("cloud settings persist non-secret metadata and reject unsafe identifiers") {
    TestDirectory directory;
    wowai::storage::LocalDatabase database{directory.path() / L"assistant.db"};
    auto settings = database.load_settings();
    settings.cloud_enabled = true;
    settings.cloud_provider = "dashscope";
    settings.cloud_model = "qwen3.5-plus";
    settings.cloud_profile = "raid-lead";
    settings.cloud_region = "singapore";
    settings.cloud_resource = "workspace-42";
    settings.cloud_session_request_limit = 5;
    settings.cloud_daily_request_limit = 20;
    settings.cloud_monthly_request_limit = 200;
    REQUIRE(settings.valid());
    database.save_settings(settings);
    const auto reopened = database.load_settings();
    CHECK(reopened.cloud_provider == "dashscope");
    CHECK(reopened.cloud_model == "qwen3.5-plus");
    CHECK(reopened.cloud_profile == "raid-lead");
    CHECK(reopened.cloud_resource == "workspace-42");
    CHECK(reopened.cloud_daily_request_limit == 20);

    settings.cloud_profile = "../escape";
    CHECK_FALSE(settings.valid());
    settings.cloud_profile = "raid-lead";
    settings.cloud_resource = "Uppercase";
    CHECK_FALSE(settings.valid());
    settings.cloud_resource = "workspace-42";
    settings.cloud_region = "unknown";
    CHECK_FALSE(settings.valid());
}

TEST_CASE("cloud master switch prevents secure session startup") {
    TestDirectory directory;
    wowai::storage::CredentialStore credentials{directory.path() / L"Credentials"};
    auto settings = wowai::storage::AssistantSettings::defaults();
    settings.cloud_enabled = false;
    CHECK(wowai::codex::AssistantSession::from_secure_settings(settings, credentials) == nullptr);
}

TEST_CASE("cloud request audit enforces idempotency and request caps without content") {
    TestDirectory directory;
    wowai::storage::LocalDatabase database{directory.path() / L"assistant.db"};
    auto settings = database.load_settings();
    settings.cloud_enabled = true;
    settings.cloud_provider = "xai";
    settings.cloud_model = "grok-4.7";
    settings.cloud_session_request_limit = 2;
    settings.cloud_daily_request_limit = 3;
    settings.cloud_monthly_request_limit = 4;
    database.save_settings(settings);
    using Authorization = wowai::storage::CloudRequestAuthorization;
    CHECK(database.authorize_cloud_request(settings, 0, "request-one", "api.x.ai", true) ==
          Authorization::allowed);
    CHECK(database.authorize_cloud_request(settings, 1, "request-one", "api.x.ai", true) ==
          Authorization::duplicate);
    CHECK(database.authorize_cloud_request(settings, 1, "request-two", "api.x.ai", false) ==
          Authorization::allowed);
    CHECK(database.authorize_cloud_request(settings, 2, "request-three", "api.x.ai", false) ==
          Authorization::session_limit);
    CHECK(database.cloud_audit_count() == 2);
    const auto bytes = read_binary(database.path());
    CHECK(bytes.find("question body") == std::string::npos);
    CHECK(bytes.find("image/png;base64") == std::string::npos);
}

TEST_CASE("temporary screenshot cleanup stays inside the application root") {
    TestDirectory directory;
    const auto paths = wowai::storage::ApplicationPaths::under(directory.path() / L"app");
    paths.create_private_directories();
    std::ofstream{paths.screenshots / L"frame.png"} << "sensitive-image";
    std::filesystem::create_directories(paths.screenshots / L"nested");
    std::ofstream{paths.screenshots / L"nested" / L"frame.tmp"} << "bytes";
    const auto outside = directory.path() / L"outside.txt";
    std::ofstream{outside} << "keep";
    wowai::storage::LocalDataCleaner cleaner{paths};
    CHECK(cleaner.clean_temporary_screenshots() >= 3);
    CHECK(std::filesystem::is_empty(paths.screenshots));
    CHECK(std::filesystem::exists(outside));
    CHECK_FALSE(wowai::storage::is_within(paths.root, directory.path()));
}

TEST_CASE("diagnostic redaction removes credentials headers and image payloads") {
    const auto result =
        wowai::storage::redact_diagnostic("Authorization: Bearer abcdefgh token=secret-token "
                                          "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAE",
                                          {"secret-token"});
    CHECK(result.find("abcdefgh") == std::string::npos);
    CHECK(result.find("secret-token") == std::string::npos);
    CHECK(result.find("iVBORw0") == std::string::npos);
    CHECK(result.find("[REDACTED]") != std::string::npos);
}

TEST_CASE("rotating safe log never writes supplied credential plaintext") {
    TestDirectory directory;
    constexpr std::string_view secret = "sk-sensitive-log-value-9981";
    {
        wowai::storage::SafeLog log{directory.path()};
        log.error("provider.failure", "Authorization: Bearer sk-sensitive-log-value-9981");
        log.info("credential.test", secret);
        log.flush();
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory.path())) {
        if (entry.is_regular_file()) {
            CHECK(read_binary(entry.path()).find(secret) == std::string::npos);
        }
    }
}
