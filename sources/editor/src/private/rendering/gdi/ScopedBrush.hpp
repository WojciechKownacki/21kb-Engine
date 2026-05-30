#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct ScopedBrush {
    explicit ScopedBrush(COLORREF color)
        : handle(CreateSolidBrush(color)) {}

    ~ScopedBrush() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedBrush(const ScopedBrush&) = delete;
    ScopedBrush& operator=(const ScopedBrush&) = delete;

    HBRUSH handle = nullptr;
};

#endif

} // namespace kb::editor
