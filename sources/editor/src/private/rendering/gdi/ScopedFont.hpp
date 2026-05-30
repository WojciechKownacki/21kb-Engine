#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

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
              L"Segoe UI")) {}

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
