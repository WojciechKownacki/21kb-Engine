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

class SvgPathGdiplusPoint {
public:
    SvgPathGdiplusPoint() = delete;

    [[nodiscard]] static Gdiplus::PointF From(SvgPathPoint point) noexcept {
        return Gdiplus::PointF(static_cast<Gdiplus::REAL>(point.x), static_cast<Gdiplus::REAL>(point.y));
    }
};

#endif

} // namespace kb::editor
