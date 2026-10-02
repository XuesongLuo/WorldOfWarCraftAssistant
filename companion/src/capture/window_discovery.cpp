#include "wowai/capture/window_discovery.hpp"

#include "wowai/platform/resources.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace wowai::capture {
namespace {

[[nodiscard]] std::wstring lowercase(std::wstring value) {
    std::ranges::transform(value, value.begin(),
                           [](const wchar_t value) { return std::towlower(value); });
    return value;
}

[[nodiscard]] std::uint64_t fnv1a(const std::wstring_view value) noexcept {
    constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t result = offset_basis;
    for (const wchar_t character : value) {
        const auto normalized = static_cast<std::uint16_t>(std::towlower(character));
        result ^= normalized & 0xffU;
        result *= prime;
        result ^= normalized >> 8U;
        result *= prime;
    }
    return result;
}

[[nodiscard]] std::optional<std::wstring> process_image_path(const DWORD process_id) {
    wowai::platform::UniqueHandle process{
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id)};
    if (!process) {
        return std::nullopt;
    }

    std::wstring buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    if (::QueryFullProcessImageNameW(process.get(), 0, buffer.data(), &length) == FALSE ||
        length == 0) {
        return std::nullopt;
    }
    buffer.resize(length);
    return buffer;
}

[[nodiscard]] std::optional<WindowCandidate> inspect_window(const HWND window) {
    if (::IsWindowVisible(window) == FALSE || ::GetWindow(window, GW_OWNER) != nullptr ||
        (::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return std::nullopt;
    }

    DWORD process_id = 0;
    ::GetWindowThreadProcessId(window, &process_id);
    const auto image_path = process_image_path(process_id);
    if (!image_path) {
        return std::nullopt;
    }
    const std::filesystem::path path{*image_path};
    const std::wstring executable_name = path.filename().wstring();
    if (!is_supported_wow_executable(executable_name)) {
        return std::nullopt;
    }

    RECT client{};
    if (::GetClientRect(window, &client) == FALSE || client.right <= client.left ||
        client.bottom <= client.top) {
        return std::nullopt;
    }
    POINT origin{client.left, client.top};
    if (::ClientToScreen(window, &origin) == FALSE) {
        return std::nullopt;
    }

    std::array<wchar_t, 256> class_name{};
    const int class_length =
        ::GetClassNameW(window, class_name.data(), static_cast<int>(class_name.size()));
    if (class_length <= 0) {
        return std::nullopt;
    }

    std::array<wchar_t, 512> title{};
    const int title_length = ::GetWindowTextW(window, title.data(), static_cast<int>(title.size()));

    const std::wstring normalized_path = lowercase(path.lexically_normal().wstring());
    WindowCandidate candidate;
    candidate.window = window;
    candidate.process_id = process_id;
    candidate.identity = {lowercase(executable_name),
                          std::wstring(class_name.data(), static_cast<std::size_t>(class_length)),
                          fnv1a(normalized_path)};
    if (title_length > 0) {
        candidate.title.assign(title.data(), static_cast<std::size_t>(title_length));
    }
    candidate.client_rect_screen = {origin.x, origin.y, origin.x + client.right,
                                    origin.y + client.bottom};
    return candidate;
}

struct EnumerationContext {
    std::vector<WindowCandidate> candidates;
    bool allocation_failed{false};
};

BOOL CALLBACK enumerate_window(const HWND window, const LPARAM parameter) noexcept {
    auto* context = reinterpret_cast<EnumerationContext*>(parameter);
    try {
        if (auto candidate = inspect_window(window)) {
            context->candidates.push_back(std::move(*candidate));
        }
        return TRUE;
    } catch (...) {
        context->allocation_failed = true;
        return FALSE;
    }
}

} // namespace

bool is_supported_wow_executable(const std::wstring_view executable_name) noexcept {
    std::wstring normalized{executable_name};
    std::ranges::transform(normalized, normalized.begin(),
                           [](const wchar_t value) { return std::towlower(value); });
    return normalized == L"wow.exe" || normalized == L"wowclassic.exe" ||
           normalized == L"wowclassict.exe";
}

SelectionResult choose_window(const std::vector<WindowCandidate>& candidates,
                              const std::optional<WindowIdentity>& preferred) noexcept {
    if (candidates.empty()) {
        return {};
    }
    if (candidates.size() == 1) {
        return {SelectionKind::selected, 0};
    }
    if (preferred) {
        std::optional<std::size_t> match;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (candidates[index].identity == *preferred) {
                if (match) {
                    return {SelectionKind::requires_user, std::nullopt};
                }
                match = index;
            }
        }
        if (match) {
            return {SelectionKind::selected, match};
        }
    }
    return {SelectionKind::requires_user, std::nullopt};
}

std::vector<WindowCandidate> WindowDiscovery::discover() const {
    EnumerationContext context;
    if (::EnumWindows(enumerate_window, reinterpret_cast<LPARAM>(&context)) == FALSE &&
        context.allocation_failed) {
        throw std::runtime_error("failed to enumerate windows");
    }
    return context.candidates;
}

} // namespace wowai::capture
