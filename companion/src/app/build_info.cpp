#include "wowai/app/build_info.hpp"

namespace wowai::app {

std::string_view application_name() noexcept {
    return "World of Warcraft AI Assistant";
}

std::string_view application_version() noexcept {
    return "0.1.0-dev";
}

}  // namespace wowai::app
