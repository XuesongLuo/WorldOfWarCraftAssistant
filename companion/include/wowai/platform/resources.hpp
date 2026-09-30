#pragma once

#include <wil/com.h>
#include <wil/resource.h>

namespace wowai::platform {

using UniqueHandle = wil::unique_handle;
using UniqueThreadHandle = wil::unique_handle;
using UniqueWindow = wil::unique_hwnd;
using UniqueMenu = wil::unique_hmenu;
using UniqueIcon = wil::unique_hicon;
using ComApartment = wil::unique_couninitialize_call;

template <typename Interface> using ComPtr = wil::com_ptr<Interface>;

[[nodiscard]] inline ComApartment initialize_sta() {
    return wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
}

} // namespace wowai::platform
