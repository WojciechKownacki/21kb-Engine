#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <string>

namespace kb::editor {

#if defined(_WIN32)
namespace detail {

[[nodiscard]] inline bool LoadEditorBrandFont() {
    std::wstring executablePath(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        if (written == 0U) {
            return false;
        }
        if (written < executablePath.size() - 1U) {
            executablePath.resize(written);
            break;
        }
        executablePath.resize(executablePath.size() * 2U);
    }

    const std::wstring::size_type separator = executablePath.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return false;
    }

    const std::wstring fontPath = executablePath.substr(0U, separator + 1U) + L"Content\\EditorShell\\Fonts\\DejaVuSans.ttf";
    return AddFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr) > 0;
}

[[nodiscard]] inline const wchar_t* EditorUiFontFamily() {
    static const bool loaded = LoadEditorBrandFont();
    return loaded ? L"DejaVu Sans" : L"Segoe UI";
}

} // namespace detail

struct ScopedFont {
    ScopedFont(int pointSize, int weight)
        : handle(CreateFontW(
              -pointSize,
              0,
              0,
              0,
              weight,
              FALSE,
              FALSE,
              FALSE,
              DEFAULT_CHARSET,
              OUT_DEFAULT_PRECIS,
              CLIP_DEFAULT_PRECIS,
              CLEARTYPE_QUALITY,
              DEFAULT_PITCH | FF_DONTCARE,
              detail::EditorUiFontFamily())) {}

    ~ScopedFont() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;

    HFONT handle = nullptr;
};

#endif

} // namespace kb::editor
