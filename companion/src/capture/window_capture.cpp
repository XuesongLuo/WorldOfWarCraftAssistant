#include "wowai/capture/window_capture.hpp"

#include "wowai/platform/resources.hpp"

#include <limits>
#include <stdexcept>
#include <system_error>

#include <windows.h>

namespace wowai::capture {
namespace {

[[noreturn]] void throw_last_error(const char* operation) {
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), operation);
}

} // namespace

BgraImageView CapturedFrame::view() const noexcept {
    return {bgra_pixels, width, height, width * 4};
}

CapturedFrame WindowCapture::capture_client(const HWND window) const {
    if (window == nullptr || ::IsWindow(window) == FALSE || ::IsIconic(window) != FALSE) {
        throw std::invalid_argument("capture requires a valid, non-minimized window");
    }

    RECT client{};
    if (::GetClientRect(window, &client) == FALSE) {
        throw_last_error("GetClientRect failed");
    }
    const auto width = client.right - client.left;
    const auto height = client.bottom - client.top;
    if (width <= 0 || height <= 0 ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
            3840ULL * 2160ULL * 2ULL) {
        throw std::runtime_error("capture dimensions are invalid or exceed the safety limit");
    }

    auto source = wil::GetDC(window);
    if (!source) {
        throw_last_error("GetDC failed");
    }
    wowai::platform::UniqueDeviceContext memory{::CreateCompatibleDC(source.get())};
    if (!memory) {
        throw_last_error("CreateCompatibleDC failed");
    }
    wowai::platform::UniqueBitmap bitmap{::CreateCompatibleBitmap(source.get(), width, height)};
    if (!bitmap) {
        throw_last_error("CreateCompatibleBitmap failed");
    }
    auto selected = wil::SelectObject(memory.get(), bitmap.get());
    if (!selected) {
        throw_last_error("SelectObject failed");
    }
    if (::BitBlt(memory.get(), 0, 0, width, height, source.get(), 0, 0, SRCCOPY | CAPTUREBLT) ==
        FALSE) {
        throw_last_error("BitBlt failed");
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    CapturedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.bgra_pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                             4U);
    if (::GetDIBits(memory.get(), bitmap.get(), 0, static_cast<UINT>(height),
                    frame.bgra_pixels.data(), &info, DIB_RGB_COLORS) == 0) {
        throw_last_error("GetDIBits failed");
    }
    return frame;
}

} // namespace wowai::capture
