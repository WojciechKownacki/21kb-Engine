#include "HubText.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cwctype>

namespace kb::hub {

std::string HubText::WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring HubText::Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring HubText::PathLabel(const std::filesystem::path& path) {
    return path.empty() ? std::wstring{} : path.wstring();
}

std::wstring HubText::SanitizeProjectName(std::wstring name) {
    for (wchar_t& character : name) {
        if (!std::iswalnum(character) && character != L'-' && character != L'_') {
            character = L'_';
        }
    }
    return name.empty() ? std::wstring{ L"Project" } : name;
}

} // namespace kb::hub
