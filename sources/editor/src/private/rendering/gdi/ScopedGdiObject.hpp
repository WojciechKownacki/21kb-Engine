#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class ScopedGdiObject {
public:
    ScopedGdiObject(HDC dc, HGDIOBJ object) : dc_(dc), previous_(SelectObject(dc, object)) {}

    ~ScopedGdiObject() {
        if (dc_ != nullptr && previous_ != nullptr) {
            SelectObject(dc_, previous_);
        }
    }

    ScopedGdiObject(const ScopedGdiObject&) = delete;
    ScopedGdiObject& operator=(const ScopedGdiObject&) = delete;

private:
    HDC dc_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};

#endif

} // namespace kb::editor
