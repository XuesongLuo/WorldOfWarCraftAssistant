#pragma once

#include "wowai/capture/window_discovery.hpp"

#include <filesystem>
#include <optional>

namespace wowai::capture {

class SelectionStore final {
  public:
    explicit SelectionStore(std::filesystem::path path);

    [[nodiscard]] std::optional<WindowIdentity> load() const;
    void save(const WindowIdentity& identity) const;
    void clear() const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    [[nodiscard]] static std::filesystem::path default_path();

  private:
    std::filesystem::path path_;
};

} // namespace wowai::capture
