#include "wowai/capture/image_encoding.hpp"

#include "wowai/capture/image_processing.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <windows.h>
#include <bcrypt.h>
#include <objidl.h>
#include <wincodec.h>
#include <wil/com.h>
#include <wil/resource.h>

namespace wowai::capture {
namespace {

class ComApartment final {
  public:
    ComApartment() {
        const HRESULT result = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(result)) {
            uninitialize_ = true;
        } else if (result != RPC_E_CHANGED_MODE) {
            THROW_IF_FAILED(result);
        }
    }
    ~ComApartment() {
        if (uninitialize_) {
            ::CoUninitialize();
        }
    }

  private:
    bool uninitialize_{};
};

[[nodiscard]] std::string base64_encode(const std::vector<std::uint8_t>& bytes) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((bytes.size() + 2U) / 3U * 4U);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 3U) {
        const std::uint32_t first = bytes[offset];
        const std::uint32_t second = offset + 1U < bytes.size() ? bytes[offset + 1U] : 0U;
        const std::uint32_t third = offset + 2U < bytes.size() ? bytes[offset + 2U] : 0U;
        const std::uint32_t value = (first << 16U) | (second << 8U) | third;
        result.push_back(alphabet[(value >> 18U) & 63U]);
        result.push_back(alphabet[(value >> 12U) & 63U]);
        result.push_back(offset + 1U < bytes.size() ? alphabet[(value >> 6U) & 63U] : '=');
        result.push_back(offset + 2U < bytes.size() ? alphabet[value & 63U] : '=');
    }
    return result;
}

[[nodiscard]] std::string sha256(const std::vector<std::uint8_t>& bytes) {
    BCRYPT_ALG_HANDLE algorithm{};
    THROW_IF_NTSTATUS_FAILED(::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                                           nullptr, 0));
    wil::unique_bcrypt_algorithm close_algorithm{algorithm};
    DWORD object_length{};
    DWORD returned{};
    THROW_IF_NTSTATUS_FAILED(::BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                                 reinterpret_cast<PUCHAR>(&object_length),
                                                 sizeof(object_length), &returned, 0));
    std::vector<std::uint8_t> object(object_length);
    BCRYPT_HASH_HANDLE hash{};
    THROW_IF_NTSTATUS_FAILED(::BCryptCreateHash(algorithm, &hash, object.data(), object_length,
                                                nullptr, 0, 0));
    wil::unique_bcrypt_hash close_hash{hash};
    THROW_IF_NTSTATUS_FAILED(::BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()),
                                              static_cast<ULONG>(bytes.size()), 0));
    std::array<std::uint8_t, 32> digest{};
    THROW_IF_NTSTATUS_FAILED(
        ::BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (const auto byte : digest) {
        result << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return result.str();
}

} // namespace

EncodedImage encode_png(const BgraImageView image) {
    if (!image.valid() || image.width <= 0 || image.height <= 0 ||
        static_cast<std::uint64_t>(image.stride) * image.height > image.pixels.size()) {
        throw std::invalid_argument("PNG encoding requires a valid BGRA image");
    }
    ComApartment apartment;
    wil::com_ptr<IWICImagingFactory> factory;
    THROW_IF_FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&factory)));
    wil::com_ptr<IWICBitmap> bitmap;
    THROW_IF_FAILED(factory->CreateBitmapFromMemory(
        static_cast<UINT>(image.width), static_cast<UINT>(image.height), GUID_WICPixelFormat32bppBGRA,
        static_cast<UINT>(image.stride), static_cast<UINT>(image.stride * image.height),
        const_cast<BYTE*>(image.pixels.data()), &bitmap));

    wil::com_ptr<IStream> stream;
    THROW_IF_FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, &stream));
    wil::com_ptr<IWICBitmapEncoder> encoder;
    THROW_IF_FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    THROW_IF_FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    wil::com_ptr<IWICBitmapFrameEncode> frame;
    wil::com_ptr<IPropertyBag2> properties;
    THROW_IF_FAILED(encoder->CreateNewFrame(&frame, &properties));
    THROW_IF_FAILED(frame->Initialize(properties.get()));
    THROW_IF_FAILED(frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height)));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    THROW_IF_FAILED(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA) {
        throw std::runtime_error("WIC PNG encoder changed the requested pixel format");
    }
    THROW_IF_FAILED(frame->WriteSource(bitmap.get(), nullptr));
    THROW_IF_FAILED(frame->Commit());
    THROW_IF_FAILED(encoder->Commit());

    HGLOBAL memory{};
    THROW_IF_FAILED(::GetHGlobalFromStream(stream.get(), &memory));
    const SIZE_T size = ::GlobalSize(memory);
    if (size == 0 || size > maximum_inline_image_bytes) {
        throw EncodedImageTooLarge{};
    }
    const void* locked = ::GlobalLock(memory);
    if (locked == nullptr) {
        throw std::runtime_error("could not access the encoded screenshot");
    }
    struct Unlock final {
        HGLOBAL memory;
        ~Unlock() { ::GlobalUnlock(memory); }
    } unlock{memory};
    EncodedImage result;
    result.bytes.assign(static_cast<const std::uint8_t*>(locked),
                        static_cast<const std::uint8_t*>(locked) + size);
    result.mime_type = "image/png";
    result.sha256 = sha256(result.bytes);
    result.base64 = base64_encode(result.bytes);
    result.width = image.width;
    result.height = image.height;
    return result;
}

EncodedImage encode_png_bounded(const BgraImageView image, const std::int32_t maximum_long_edge,
                                const std::int32_t minimum_long_edge) {
    if (maximum_long_edge <= 0 || minimum_long_edge <= 0 ||
        minimum_long_edge > maximum_long_edge) {
        throw std::invalid_argument("bounded PNG edges are invalid");
    }
    std::int32_t edge = maximum_long_edge;
    while (true) {
        auto resized = resize_image(image, edge);
        try {
            return encode_png(resized.view());
        } catch (const EncodedImageTooLarge&) {
            if (edge == minimum_long_edge) {
                throw;
            }
            edge = std::max(minimum_long_edge, edge * 3 / 4);
        }
    }
}

} // namespace wowai::capture
