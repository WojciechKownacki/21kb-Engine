#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace kb::editor {

std::wstring ScriptEditorTextEncoding::Widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (required <= 0) {
        return std::wstring{ utf8.begin(), utf8.end() };
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), required);
    return wide;
}

std::string ScriptEditorTextEncoding::Narrow(std::wstring_view utf16) {
    if (utf16.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string narrow(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), narrow.data(), required, nullptr, nullptr);
    return narrow;
}

} // namespace kb::editor

#endif
