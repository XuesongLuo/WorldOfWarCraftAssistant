#include "wowai/storage/local_data.hpp"

#include <stdexcept>

namespace wowai::storage {

bool is_within(const std::filesystem::path& root, const std::filesystem::path& candidate) noexcept {
    try {
        const auto normalized_root = std::filesystem::absolute(root).lexically_normal();
        const auto normalized_candidate = std::filesystem::absolute(candidate).lexically_normal();
        auto root_it = normalized_root.begin();
        auto candidate_it = normalized_candidate.begin();
        for (; root_it != normalized_root.end(); ++root_it, ++candidate_it) {
            if (candidate_it == normalized_candidate.end() || *root_it != *candidate_it) {
                return false;
            }
        }
        return candidate_it != normalized_candidate.end();
    } catch (...) {
        return false;
    }
}

std::uintmax_t clear_directory_contents(const std::filesystem::path& directory) {
    if (directory.empty() || directory == directory.root_path()) {
        throw std::invalid_argument("refusing to clear a broad directory");
    }
    if (!std::filesystem::exists(directory)) {
        return 0;
    }
    std::uintmax_t removed{};
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        removed += std::filesystem::remove_all(entry.path());
    }
    return removed;
}

LocalDataCleaner::LocalDataCleaner(ApplicationPaths paths) : paths_(std::move(paths)) {
    if (!is_within(paths_.root, paths_.screenshots) || !is_within(paths_.root, paths_.logs) ||
        !is_within(paths_.root, paths_.credentials) || !is_within(paths_.root, paths_.overlay_ui) ||
        !is_within(paths_.root, paths_.database) ||
        !is_within(paths_.root, paths_.window_selection)) {
        throw std::invalid_argument("local data paths must remain below the application root");
    }
}

std::uintmax_t LocalDataCleaner::clean_temporary_screenshots() const {
    return clear_directory_contents(paths_.screenshots);
}

std::uintmax_t LocalDataCleaner::delete_non_database_data() const {
    std::uintmax_t removed{};
    removed += clear_directory_contents(paths_.screenshots);
    removed += clear_directory_contents(paths_.logs);
    removed += clear_directory_contents(paths_.credentials);
    removed += clear_directory_contents(paths_.overlay_ui);
    std::error_code ignored;
    if (std::filesystem::remove(paths_.window_selection, ignored)) {
        ++removed;
    }
    for (const auto& entry : std::filesystem::directory_iterator(paths_.root)) {
        const auto name = entry.path().filename().wstring();
        if (entry.is_regular_file() && name.rfind(L"assistant.db.corrupt-", 0) == 0) {
            ignored.clear();
            if (std::filesystem::remove(entry.path(), ignored)) {
                ++removed;
            }
        }
    }
    return removed;
}

} // namespace wowai::storage
