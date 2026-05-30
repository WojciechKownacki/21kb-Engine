#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class GdiAlphaBlender {
public:
    GdiAlphaBlender() = delete;

    static void Fill(HDC target, const RECT& rect, COLORREF color, BYTE alpha);
};

#endif

} // namespace kb::editor
