#pragma once

#include "wowai/storage/application_paths.hpp"

#include <cstdint>
#include <filesystem>

namespace wowai::storage {

[[nodiscard]] std::uintmax_t clear_directory_contents(const std::filesystem::path& directory);
[[nodiscard]] bool is_within(const std::filesystem::path& root,
                             const std::filesystem::path& candidate) noexcept;

class LocalDataCleaner final {
  public:
    explicit LocalDataCleaner(ApplicationPaths paths);

    [[nodiscard]] std::uintmax_t clean_temporary_screenshots() const;
    [[nodiscard]] std::uintmax_t delete_non_database_data() const;

  private:
    ApplicationPaths paths_;
};

} // namespace wowai::storage
