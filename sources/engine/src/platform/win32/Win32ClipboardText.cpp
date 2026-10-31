#include "engine/platform/win32/Win32ClipboardText.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>

namespace kb::platform {
namespace {
constexpr std::size_t kMaximumBytes = 1024U * 1024U;
struct ClipboardScope {
    bool opened;
    explicit ClipboardScope(void* owner) : opened(OpenClipboard(static_cast<HWND>(owner)) != FALSE) {}
    ~ClipboardScope() { if (opened) CloseClipboard(); }
};
struct LockedText {
    HGLOBAL memory;
    const wchar_t* text;
    explicit LockedText(HGLOBAL value) : memory(value), text(static_cast<const wchar_t*>(GlobalLock(value))) {}
    ~LockedText() { if (text != nullptr) GlobalUnlock(memory); }
};
}

bool WriteWin32ClipboardText(void* owner, std::string_view text) noexcept {
    if (text.size() > kMaximumBytes || text.find('\0') != std::string_view::npos) return false;
    const int length = text.empty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!text.empty() && length == 0) return false;
    const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (static_cast<std::size_t>(length) + 1U) * sizeof(wchar_t));
    if (memory == nullptr) return false;
    auto* output = static_cast<wchar_t*>(GlobalLock(memory));
    if (output == nullptr) { GlobalFree(memory); return false; }
    const bool converted = length == 0 || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), output, length) == length;
    output[length] = L'\0';
    GlobalUnlock(memory);
    if (!converted) { GlobalFree(memory); return false; }
    ClipboardScope clipboard{owner};
    if (!clipboard.opened || !EmptyClipboard() || SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        return false;
    }
    return true;
}

std::optional<std::string> ReadWin32ClipboardText(void* owner, std::size_t maximumBytes) {
    ClipboardScope clipboard{owner};
    if (!clipboard.opened) return std::nullopt;
    const HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    if (memory == nullptr) return std::nullopt;
    const auto bytes = GlobalSize(memory);
    if (bytes < sizeof(wchar_t)) return std::nullopt;
    const LockedText locked{memory};
    if (locked.text == nullptr) return std::nullopt;
    // GlobalSize may include allocator padding; bound the scan, not the handle.
    const auto capacity = std::min(bytes / sizeof(wchar_t), kMaximumBytes + 1U);
    const auto* terminator = std::find(locked.text, locked.text + capacity, L'\0');
    if (terminator == locked.text + capacity) return std::nullopt;
    const auto length = static_cast<int>(terminator - locked.text);
    if (length == 0) return std::string{};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, locked.text, length, nullptr, 0, nullptr, nullptr);
    if (required <= 0 || static_cast<std::size_t>(required) > kMaximumBytes) return std::nullopt;
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, locked.text, length,
        result.data(), required, nullptr, nullptr) != required) return std::nullopt;
    if (result.size() > maximumBytes) {
        auto boundary = maximumBytes;
        while (boundary > 0U && (static_cast<unsigned char>(result[boundary]) & 0xC0U) == 0x80U) --boundary;
        result.resize(boundary);
    }
    return result;
}
} // namespace kb::platform
