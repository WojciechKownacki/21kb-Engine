#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct ScopedCompatibleDc {
    explicit ScopedCompatibleDc(HDC source)
        : handle(CreateCompatibleDC(source)) {}

    ~ScopedCompatibleDc() {
        if (handle != nullptr) {
            DeleteDC(handle);
        }
    }

    ScopedCompatibleDc(const ScopedCompatibleDc&) = delete;
    ScopedCompatibleDc& operator=(const ScopedCompatibleDc&) = delete;

    HDC handle = nullptr;
};

#endif

} // namespace kb::editor
