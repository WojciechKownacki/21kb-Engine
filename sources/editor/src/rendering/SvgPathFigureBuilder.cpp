#include "rendering/SvgPathFigureBuilder.hpp"

#include "rendering/SvgPathArcConverter.hpp"
#include "rendering/SvgPathGdiplusPoint.hpp"

#if defined(_WIN32)

namespace kb::editor {

SvgPathFigureBuilder::SvgPathFigureBuilder(Gdiplus::GraphicsPath& path) : path_(path) {}

void SvgPathFigureBuilder::Move(double x, double y, bool relative) {
    current_ = MakePoint(x, y, relative);
    subpathStart_ = current_;
    path_.StartFigure();
}

void SvgPathFigureBuilder::Line(double x, double y, bool relative) {
    const SvgPathPoint next = MakePoint(x, y, relative);
    path_.AddLine(SvgPathGdiplusPoint::From(current_), SvgPathGdiplusPoint::From(next));
    current_ = next;
}

void SvgPathFigureBuilder::Horizontal(double x, bool relative) {
    const SvgPathPoint next{ relative ? current_.x + x : x, current_.y };
    path_.AddLine(SvgPathGdiplusPoint::From(current_), SvgPathGdiplusPoint::From(next));
    current_ = next;
}

void SvgPathFigureBuilder::Vertical(double y, bool relative) {
    const SvgPathPoint next{ current_.x, relative ? current_.y + y : y };
    path_.AddLine(SvgPathGdiplusPoint::From(current_), SvgPathGdiplusPoint::From(next));
    current_ = next;
}

void SvgPathFigureBuilder::Cubic(double c1x, double c1y, double c2x, double c2y, double x, double y, bool relative) {
    const SvgPathPoint c1 = MakePoint(c1x, c1y, relative);
    const SvgPathPoint c2 = MakePoint(c2x, c2y, relative);
    const SvgPathPoint next = MakePoint(x, y, relative);
    path_.AddBezier(
        SvgPathGdiplusPoint::From(current_),
        SvgPathGdiplusPoint::From(c1),
        SvgPathGdiplusPoint::From(c2),
        SvgPathGdiplusPoint::From(next));
    current_ = next;
}

void SvgPathFigureBuilder::Arc(
    double rx,
    double ry,
    double rotationDegrees,
    bool largeArc,
    bool sweep,
    double x,
    double y,
    bool relative) {
    const SvgPathPoint next = MakePoint(x, y, relative);
    SvgPathArcConverter::AddArc(path_, current_, next, rx, ry, rotationDegrees, largeArc, sweep);
    current_ = next;
}

void SvgPathFigureBuilder::Close() {
    path_.CloseFigure();
    current_ = subpathStart_;
}

SvgPathPoint SvgPathFigureBuilder::MakePoint(double x, double y, bool relative) const noexcept {
    return relative ? SvgPathPoint{ current_.x + x, current_.y + y } : SvgPathPoint{ x, y };
}

} // namespace kb::editor

#endif
