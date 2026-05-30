#pragma once

#if defined(_WIN32)
#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)
#endif

namespace kb::editor {

#if defined(_WIN32)

class SvgPathArcSegment {
public:
    SvgPathArcSegment() = delete;

    static void AddBezierSegment(
        Gdiplus::GraphicsPath& path,
        double cx,
        double cy,
        double rx,
        double ry,
        double phi,
        double theta1,
        double theta2);
};

#endif

} // namespace kb::editor
