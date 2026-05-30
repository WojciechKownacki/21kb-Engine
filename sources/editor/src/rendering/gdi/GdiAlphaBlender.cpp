#include "rendering/gdi/GdiAlphaBlender.hpp"

#include "rendering/gdi/GdiRectPainter.hpp"
#include "rendering/gdi/ScopedBitmap.hpp"
#include "rendering/gdi/ScopedCompatibleDc.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#if defined(_WIN32)

namespace kb::editor {

void GdiAlphaBlender::Fill(HDC target, const RECT& rect, COLORREF color, BYTE alpha) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    ScopedCompatibleDc overlayDc(target);
    ScopedBitmap overlayBitmap(target, width, height);
    const ScopedGdiObject selectedBitmap(overlayDc.handle, overlayBitmap.handle);

    RECT overlayRect{ 0, 0, width, height };
    GdiRectPainter::Fill(overlayDc.handle, overlayRect, color);
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = alpha;
    AlphaBlend(target, rect.left, rect.top, width, height, overlayDc.handle, 0, 0, width, height, blend);
}

} // namespace kb::editor

#endif
