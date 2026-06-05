#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct GdiBackBufferPaintContext {
    HDC dc = nullptr;
    RECT client{};
    RECT dirty{};
    int width = 0;
    int height = 0;
};

using GdiBackBufferPaintFn = void (*)(const GdiBackBufferPaintContext& paint, void* context);

class GdiBackBufferRenderer {
public:
    GdiBackBufferRenderer() = delete;

    static void Paint(HWND window, GdiBackBufferPaintFn paint, void* context);
};

#endif

} // namespace kb::editor
