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

    // While one of these lives, Paint for its window draws the whole client into `target`
    // instead of the screen: how a window answers WM_PRINTCLIENT with what it would paint.
    class PrintScope final {
    public:
        PrintScope(HWND window, HDC target) noexcept;
        ~PrintScope();
        PrintScope(const PrintScope&) = delete;
        PrintScope& operator=(const PrintScope&) = delete;
    };
};

#endif

} // namespace kb::editor
