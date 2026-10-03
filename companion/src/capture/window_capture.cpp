#include "wowai/capture/window_capture.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <system_error>

#include <d3d11.h>
#include <dwmapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>
#include <wrl/client.h>

namespace wowai::capture {
namespace {

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

[[noreturn]] void throw_last_error(const char* operation) {
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), operation);
}

[[nodiscard]] ComPtr<ID3D11Device> make_d3d_device() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    winrt::check_hresult(::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                             D3D11_SDK_VERSION, &device, &level, &context));
    return device;
}

[[nodiscard]] IDirect3DDevice make_winrt_device(const ComPtr<ID3D11Device>& device) {
    ComPtr<IDXGIDevice> dxgi;
    winrt::check_hresult(device.As(&dxgi));
    winrt::com_ptr<IInspectable> inspectable;
    winrt::check_hresult(::CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
    return inspectable.as<IDirect3DDevice>();
}

[[nodiscard]] GraphicsCaptureItem item_for_window(const HWND window) {
    GraphicsCaptureItem item{nullptr};
    const auto interop =
        winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    winrt::check_hresult(interop->CreateForWindow(
        window, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
    return item;
}

[[nodiscard]] ComPtr<ID3D11Texture2D> texture_from_surface(const IDirect3DSurface& surface) {
    const auto access = surface.as<
        ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    ComPtr<ID3D11Texture2D> texture;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&texture)));
    return texture;
}

[[nodiscard]] RECT capture_bounds(const HWND window) {
    RECT bounds{};
    if (FAILED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds,
                                       sizeof(bounds))) &&
        ::GetWindowRect(window, &bounds) == FALSE) {
        throw_last_error("GetWindowRect failed");
    }
    return bounds;
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
    POINT client_origin{};
    if (::GetClientRect(window, &client) == FALSE ||
        ::ClientToScreen(window, &client_origin) == FALSE) {
        throw_last_error("could not resolve the capture client rectangle");
    }
    const std::int32_t client_width = client.right - client.left;
    const std::int32_t client_height = client.bottom - client.top;
    if (client_width <= 0 || client_height <= 0 ||
        static_cast<std::uint64_t>(client_width) * client_height > 3840ULL * 2160ULL * 2ULL) {
        throw std::runtime_error("capture dimensions are invalid or exceed the safety limit");
    }

    const auto d3d_device = make_d3d_device();
    const auto winrt_device = make_winrt_device(d3d_device);
    const auto item = item_for_window(window);
    const auto item_size = item.Size();
    if (item_size.Width <= 0 || item_size.Height <= 0) {
        throw std::runtime_error("Windows Graphics Capture returned an empty item");
    }

    auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        winrt_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item_size);
    auto session = pool.CreateCaptureSession(item);
    session.IsCursorCaptureEnabled(false);

    std::mutex mutex;
    std::condition_variable arrived;
    std::optional<Direct3D11CaptureFrame> captured;
    const auto token = pool.FrameArrived([&](const Direct3D11CaptureFramePool& sender,
                                              const winrt::Windows::Foundation::IInspectable&) {
        std::scoped_lock lock{mutex};
        if (!captured) {
            captured = sender.TryGetNextFrame();
            arrived.notify_one();
        }
    });
    session.StartCapture();
    {
        std::unique_lock lock{mutex};
        if (!arrived.wait_for(lock, std::chrono::seconds{2}, [&] { return captured.has_value(); })) {
            pool.FrameArrived(token);
            session.Close();
            pool.Close();
            throw std::runtime_error("Windows Graphics Capture timed out");
        }
    }
    pool.FrameArrived(token);
    session.Close();
    pool.Close();

    const auto texture = texture_from_surface(captured->Surface());
    D3D11_TEXTURE2D_DESC source_description{};
    texture->GetDesc(&source_description);
    if (source_description.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
        source_description.Width == 0 || source_description.Height == 0) {
        throw std::runtime_error("Windows Graphics Capture returned an unsupported surface");
    }

    D3D11_TEXTURE2D_DESC staging_description = source_description;
    staging_description.Usage = D3D11_USAGE_STAGING;
    staging_description.BindFlags = 0;
    staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_description.MiscFlags = 0;
    staging_description.MipLevels = 1;
    staging_description.ArraySize = 1;
    ComPtr<ID3D11Texture2D> staging;
    winrt::check_hresult(d3d_device->CreateTexture2D(&staging_description, nullptr, &staging));
    ComPtr<ID3D11DeviceContext> context;
    d3d_device->GetImmediateContext(&context);
    context->CopyResource(staging.Get(), texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    winrt::check_hresult(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    struct Unmap final {
        ID3D11DeviceContext* context;
        ID3D11Resource* resource;
        ~Unmap() { context->Unmap(resource, 0); }
    } unmap{context.Get(), staging.Get()};

    const RECT bounds = capture_bounds(window);
    const auto surface_width = static_cast<std::int32_t>(source_description.Width);
    const auto surface_height = static_cast<std::int32_t>(source_description.Height);
    const auto raw_offset_x = static_cast<std::int32_t>(client_origin.x - bounds.left);
    const auto raw_offset_y = static_cast<std::int32_t>(client_origin.y - bounds.top);
    const std::int32_t offset_x = std::clamp(raw_offset_x, 0, surface_width);
    const std::int32_t offset_y = std::clamp(raw_offset_y, 0, surface_height);
    const std::int32_t width = std::min(client_width, surface_width - offset_x);
    const std::int32_t height = std::min(client_height, surface_height - offset_y);
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("captured surface does not contain the selected client area");
    }

    CapturedFrame result;
    result.width = width;
    result.height = height;
    result.bgra_pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    for (std::int32_t y = 0; y < height; ++y) {
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData) +
                             static_cast<std::size_t>(offset_y + y) * mapped.RowPitch +
                             static_cast<std::size_t>(offset_x) * 4U;
        auto* destination = result.bgra_pixels.data() +
                            static_cast<std::size_t>(y) * width * 4U;
        std::copy_n(source, static_cast<std::size_t>(width) * 4U, destination);
    }
    return result;
}

} // namespace wowai::capture
