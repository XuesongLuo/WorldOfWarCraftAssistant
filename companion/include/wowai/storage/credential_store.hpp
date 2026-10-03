#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace wowai::storage {

class CredentialStore final {
  public:
    explicit CredentialStore(std::filesystem::path directory);

    void write(std::string_view provider, std::string_view profile, std::string_view secret) const;
    [[nodiscard]] std::optional<std::string> read(std::string_view provider,
                                                  std::string_view profile) const;
    void erase(std::string_view provider, std::string_view profile) const;
    void write(std::string_view provider, std::string_view secret) const {
        write(provider, "default", secret);
    }
    [[nodiscard]] std::optional<std::string> read(std::string_view provider) const {
        return read(provider, "default");
    }
    void erase(std::string_view provider) const { erase(provider, "default"); }
    void erase_all() const;

    [[nodiscard]] static bool valid_provider(std::string_view provider) noexcept;
    [[nodiscard]] static bool valid_profile(std::string_view profile) noexcept;

  private:
    [[nodiscard]] std::filesystem::path path_for(std::string_view provider,
                                                 std::string_view profile) const;
    std::filesystem::path directory_;
};

} // namespace wowai::storage
