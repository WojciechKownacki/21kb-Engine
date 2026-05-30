#pragma once

#include "rendering/SvgPathPoint.hpp"

#if defined(_WIN32)
#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)
#endif

namespace kb::editor {

#if defined(_WIN32)

class SvgPathFigureBuilder {
public:
    explicit SvgPathFigureBuilder(Gdiplus::GraphicsPath& path);

    void Move(double x, double y, bool relative);
    void Line(double x, double y, bool relative);
    void Horizontal(double x, bool relative);
    void Vertical(double y, bool relative);
    void Cubic(double c1x, double c1y, double c2x, double c2y, double x, double y, bool relative);
    void Arc(double rx, double ry, double rotationDegrees, bool largeArc, bool sweep, double x, double y, bool relative);
    void Close();

private:
    [[nodiscard]] SvgPathPoint MakePoint(double x, double y, bool relative) const noexcept;

    Gdiplus::GraphicsPath& path_;
    SvgPathPoint current_{};
    SvgPathPoint subpathStart_{};
};

#endif

} // namespace kb::editor
