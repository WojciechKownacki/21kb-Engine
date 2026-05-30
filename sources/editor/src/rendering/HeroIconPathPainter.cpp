#include "rendering/HeroIconPathPainter.hpp"

#if defined(_WIN32)

namespace kb::editor {

void HeroIconPathPainter::Paint(
    Gdiplus::Graphics& graphics,
    Gdiplus::GraphicsPath& path,
    const HeroIconPath& iconPath,
    const Gdiplus::Color& color,
    float strokeWidth) {
    if (iconPath.filled) {
        Gdiplus::SolidBrush brush(color);
        graphics.FillPath(&brush, &path);
        return;
    }

    Gdiplus::Pen pen(color, strokeWidth);
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&pen, &path);
}

} // namespace kb::editor

#endif
