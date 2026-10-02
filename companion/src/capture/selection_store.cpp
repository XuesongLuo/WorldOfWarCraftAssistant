#include "wowai/capture/selection_store.hpp"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#include <windows.h>

namespace wowai::capture {
namespace {

[[nodiscard]] bool safe_field(const std::wstring_view value) noexcept {
    return !value.empty() && value.size() <= 255 && value.find_first_of(L"\r\n") == value.npos;
}

} // namespace

SelectionStore::SelectionStore(std::filesystem::path path) : path_(std::move(path)) {
    if (path_.empty()) {
        throw std::invalid_argument("selection store path must not be empty");
    }
}

std::optional<WindowIdentity> SelectionStore::load() const {
    std::wifstream input(path_);
    if (!input) {
        return std::nullopt;
    }

    WindowIdentity identity;
    std::wstring fingerprint;
    if (!std::getline(input, identity.executable_name) ||
        !std::getline(input, identity.window_class) || !std::getline(input, fingerprint) ||
        !safe_field(identity.executable_name) || !safe_field(identity.window_class) ||
        fingerprint.empty() || fingerprint.size() > 16) {
        return std::nullopt;
    }
    try {
        std::size_t parsed = 0;
        identity.installation_fingerprint = std::stoull(fingerprint, &parsed, 16);
        if (parsed != fingerprint.size() ||
            !is_supported_wow_executable(identity.executable_name)) {
            return std::nullopt;
        }
    } catch (...) {
        return std::nullopt;
    }
    return identity;
}

void SelectionStore::save(const WindowIdentity& identity) const {
    if (!safe_field(identity.executable_name) || !safe_field(identity.window_class) ||
        !is_supported_wow_executable(identity.executable_name)) {
        throw std::invalid_argument("window identity is invalid");
    }
    if (const auto parent = path_.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    const std::filesystem::path temporary = path_.wstring() + L".tmp";
    {
        std::wofstream output(temporary, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("could not open the window selection file");
        }
        output << identity.executable_name << L'\n' << identity.window_class << L'\n' << std::hex
               << identity.installation_fingerprint << L'\n';
        if (!output) {
            throw std::runtime_error("could not write the window selection file");
        }
    }
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
    std::filesystem::rename(temporary, path_);
}

void SelectionStore::clear() const {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
}

const std::filesystem::path& SelectionStore::path() const noexcept { return path_; }

std::filesystem::path SelectionStore::default_path() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(),
                                                   static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("LOCALAPPDATA is unavailable");
    }
    buffer.resize(length);
    return std::filesystem::path{buffer} / L"WorldOfWarcraftAssistant" / L"window-selection.txt";
}

} // namespace wowai::capture
