#pragma once

#include "wowai/platform/resources.hpp"

#include <string_view>

#include <windows.h>

namespace wowai::platform {

class SingleInstance final {
  public:
    explicit SingleInstance(std::wstring_view mutex_name);

    [[nodiscard]] bool is_primary() const noexcept;

    [[nodiscard]] static bool notify_existing_window(std::wstring_view window_class,
                                                     UINT activation_message,
                                                     DWORD wait_milliseconds = 2000) noexcept;

  private:
    UniqueHandle mutex_;
    bool primary_{false};
};

} // namespace wowai::platform
