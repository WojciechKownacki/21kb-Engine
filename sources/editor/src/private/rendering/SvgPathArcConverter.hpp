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

class SvgPathArcConverter {
public:
    SvgPathArcConverter() = delete;

    static void AddArc(
        Gdiplus::GraphicsPath& path,
        SvgPathPoint from,
        SvgPathPoint to,
        double rx,
        double ry,
        double rotationDegrees,
        bool largeArc,
        bool sweep);
};

#endif

} // namespace kb::editor
