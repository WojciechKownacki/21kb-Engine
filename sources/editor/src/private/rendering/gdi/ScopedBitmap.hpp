#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>

namespace kb::editor {

#if defined(_WIN32)

struct ScopedBitmap {
    ScopedBitmap(HDC source, int width, int height)
        : handle(CreateCompatibleBitmap(source, std::max(1, width), std::max(1, height))) {}

    ~ScopedBitmap() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedBitmap(const ScopedBitmap&) = delete;
    ScopedBitmap& operator=(const ScopedBitmap&) = delete;

    HBITMAP handle = nullptr;
};

#endif

} // namespace kb::editor
