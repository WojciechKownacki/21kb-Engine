#include "rendering/GdiBackBufferRenderer.hpp"

#if defined(_WIN32)
#include "rendering/gdi/ScopedBitmap.hpp"
#include "rendering/gdi/ScopedCompatibleDc.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/gdi/ScopedPaint.hpp"

namespace kb::editor {

void GdiBackBufferRenderer::Paint(HWND window, GdiBackBufferPaintFn paint, void* context) {
    if (paint == nullptr) {
        return;
    }

    ScopedPaint paintScope(window);
    HDC targetDc = paintScope.Dc();
    if (targetDc == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    ScopedCompatibleDc memoryDc(targetDc);
    ScopedBitmap backBuffer(targetDc, width, height);
    {
        const ScopedGdiObject selectedBitmap(memoryDc.handle, backBuffer.handle);
        paint(
            GdiBackBufferPaintContext{
                .dc = memoryDc.handle,
                .client = client,
                .width = width,
                .height = height,
            },
            context);

        BitBlt(targetDc, 0, 0, width, height, memoryDc.handle, 0, 0, SRCCOPY);
    }
}

} // namespace kb::editor

#endif
