#include "wowai/storage/credential_store.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <windows.h>

#include <dpapi.h>

namespace wowai::storage {
namespace {

constexpr std::string_view entropy_text = "WorldOfWarcraftAssistant/v1/provider-credential";

class LocalBytes final {
  public:
    explicit LocalBytes(BYTE* value = nullptr) noexcept : value_(value) {}
    ~LocalBytes() { ::LocalFree(value_); }
    LocalBytes(const LocalBytes&) = delete;
    LocalBytes& operator=(const LocalBytes&) = delete;
    [[nodiscard]] BYTE* get() const noexcept { return value_; }

  private:
    BYTE* value_{};
};

[[nodiscard]] DATA_BLOB entropy_blob() noexcept {
    return {static_cast<DWORD>(entropy_text.size()),
            reinterpret_cast<BYTE*>(const_cast<char*>(entropy_text.data()))};
}

} // namespace

CredentialStore::CredentialStore(std::filesystem::path directory)
    : directory_(std::move(directory)) {
    if (directory_.empty()) {
        throw std::invalid_argument("credential directory must not be empty");
    }
}

bool CredentialStore::valid_provider(const std::string_view provider) noexcept {
    return !provider.empty() && provider.size() <= 32 &&
           std::all_of(provider.begin(), provider.end(), [](const char value) {
               return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
                      value == '-';
           });
}

std::filesystem::path CredentialStore::path_for(const std::string_view provider) const {
    if (!valid_provider(provider)) {
        throw std::invalid_argument("credential provider identifier is invalid");
    }
    return directory_ / (std::string{provider} + ".dpapi");
}

void CredentialStore::write(const std::string_view provider, const std::string_view secret) const {
    if (secret.empty() || secret.size() > 16 * 1024) {
        throw std::invalid_argument("credential value is empty or too large");
    }
    std::filesystem::create_directories(directory_);
    DATA_BLOB input{static_cast<DWORD>(secret.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()))};
    DATA_BLOB output{};
    auto entropy = entropy_blob();
    if (::CryptProtectData(&input, L"World of Warcraft AI Assistant credential", &entropy, nullptr,
                           nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output) == FALSE) {
        throw std::runtime_error("Windows DPAPI could not protect the credential");
    }
    LocalBytes protected_bytes{output.pbData};
    const auto target = path_for(provider);
    const auto temporary = target.wstring() + L".tmp";
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        stream.write(reinterpret_cast<const char*>(protected_bytes.get()), output.cbData);
        if (!stream) {
            throw std::runtime_error("could not write protected credential");
        }
    }
    std::error_code ignored;
    std::filesystem::remove(target, ignored);
    std::filesystem::rename(temporary, target);
}

std::optional<std::string> CredentialStore::read(const std::string_view provider) const {
    const auto path = path_for(provider);
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return std::nullopt;
    }
    std::vector<BYTE> encrypted{std::istreambuf_iterator<char>{stream},
                                std::istreambuf_iterator<char>{}};
    if (encrypted.empty() || encrypted.size() > 64 * 1024) {
        throw std::runtime_error("protected credential file is invalid");
    }
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), encrypted.data()};
    DATA_BLOB output{};
    auto entropy = entropy_blob();
    if (::CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                             &output) == FALSE) {
        throw std::runtime_error("Windows DPAPI could not unprotect the credential");
    }
    LocalBytes plain_bytes{output.pbData};
    std::string result{reinterpret_cast<const char*>(plain_bytes.get()), output.cbData};
    ::SecureZeroMemory(plain_bytes.get(), output.cbData);
    if (result.empty()) {
        throw std::runtime_error("protected credential is empty");
    }
    return result;
}

void CredentialStore::erase(const std::string_view provider) const {
    std::error_code error;
    std::filesystem::remove(path_for(provider), error);
}

void CredentialStore::erase_all() const {
    if (!std::filesystem::exists(directory_)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
        if (entry.is_regular_file() && entry.path().extension() == L".dpapi") {
            std::error_code ignored;
            std::filesystem::remove(entry.path(), ignored);
        }
    }
}

} // namespace wowai::storage
