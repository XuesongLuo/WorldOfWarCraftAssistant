#include "wowai/storage/application_paths.hpp"

#include <stdexcept>
#include <string>

#include <windows.h>

namespace wowai::storage {

ApplicationPaths ApplicationPaths::under(std::filesystem::path root) {
    if (root.empty()) {
        throw std::invalid_argument("application data root must not be empty");
    }
    root = std::filesystem::absolute(std::move(root)).lexically_normal();
    return {root,
            root / L"assistant.db",
            root / L"Credentials",
            root / L"Logs",
            root / L"vision-temp",
            root / L"OverlayUi",
            root / L"window-selection.txt"};
}

ApplicationPaths ApplicationPaths::defaults() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(),
                                                   static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("LOCALAPPDATA is unavailable");
    }
    buffer.resize(length);
    return under(std::filesystem::path{buffer} / L"WorldOfWarcraftAssistant");
}

void ApplicationPaths::create_private_directories() const {
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(credentials);
    std::filesystem::create_directories(logs);
    std::filesystem::create_directories(screenshots);
    std::filesystem::create_directories(overlay_ui);
}

} // namespace wowai::storage
