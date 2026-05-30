#include "rendering/SvgPathArcConverter.hpp"

#if defined(_WIN32)
#include "rendering/SvgPathArcEndpoint.hpp"
#include "rendering/SvgPathArcMath.hpp"
#include "rendering/SvgPathArcSegment.hpp"
#include "rendering/SvgPathGdiplusPoint.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace kb::editor {
namespace {

constexpr double kPi = 3.14159265358979323846;

void AddCenterArcSegments(Gdiplus::GraphicsPath& path, const SvgPathArcCenter& arc) {
    const int segments = (std::max)(1, static_cast<int>(std::ceil(std::abs(arc.delta) / (kPi * 0.5))));
    const double step = arc.delta / static_cast<double>(segments);
    for (int index = 0; index < segments; ++index) {
        SvgPathArcSegment::AddBezierSegment(
            path,
            arc.cx,
            arc.cy,
            arc.rx,
            arc.ry,
            arc.phi,
            arc.theta1 + step * index,
            arc.theta1 + step * (index + 1));
    }
}

} // namespace

void SvgPathArcConverter::AddArc(
    Gdiplus::GraphicsPath& path,
    SvgPathPoint from,
    SvgPathPoint to,
    double rx,
    double ry,
    double rotationDegrees,
    bool largeArc,
    bool sweep) {
    const std::optional<SvgPathArcCenter> centerArc = SvgPathArcMath::ToCenterArc(SvgPathArcEndpoint{
        .from = from,
        .to = to,
        .rx = rx,
        .ry = ry,
        .rotationDegrees = rotationDegrees,
        .largeArc = largeArc,
        .sweep = sweep,
    });

    if (!centerArc.has_value()) {
        path.AddLine(SvgPathGdiplusPoint::From(from), SvgPathGdiplusPoint::From(to));
        return;
    }

    AddCenterArcSegments(path, *centerArc);
}

} // namespace kb::editor

#endif
