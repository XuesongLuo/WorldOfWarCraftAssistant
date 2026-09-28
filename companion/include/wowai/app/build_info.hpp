#pragma once

#include <string_view>

namespace wowai::app {

[[nodiscard]] std::string_view application_name() noexcept;
[[nodiscard]] std::string_view application_version() noexcept;

}  // namespace wowai::app
