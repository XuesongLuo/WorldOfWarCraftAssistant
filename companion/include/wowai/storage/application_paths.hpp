#pragma once

#include <filesystem>

namespace wowai::storage {

struct ApplicationPaths {
    std::filesystem::path root;
    std::filesystem::path database;
    std::filesystem::path credentials;
    std::filesystem::path logs;
    std::filesystem::path screenshots;
    std::filesystem::path overlay_ui;
    std::filesystem::path window_selection;

    [[nodiscard]] static ApplicationPaths under(std::filesystem::path root);
    [[nodiscard]] static ApplicationPaths defaults();
    void create_private_directories() const;
};

} // namespace wowai::storage
