#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct ScopedPen {
    ScopedPen(int width, COLORREF color)
        : handle(CreatePen(PS_SOLID, width, color)) {}

    ~ScopedPen() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedPen(const ScopedPen&) = delete;
    ScopedPen& operator=(const ScopedPen&) = delete;

    HPEN handle = nullptr;
};

#endif

} // namespace kb::editor
