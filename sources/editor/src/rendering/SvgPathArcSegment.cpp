#include "rendering/SvgPathArcSegment.hpp"

#include "rendering/SvgPathGdiplusPoint.hpp"
#include "rendering/SvgPathPoint.hpp"

#if defined(_WIN32)

#include <cmath>

namespace kb::editor {
namespace {

[[nodiscard]] SvgPathPoint ArcPoint(double cx, double cy, double rx, double ry, double phi, double theta) noexcept {
    const double cosPhi = std::cos(phi);
    const double sinPhi = std::sin(phi);
    const double cosTheta = std::cos(theta);
    const double sinTheta = std::sin(theta);
    return {
        cx + rx * cosPhi * cosTheta - ry * sinPhi * sinTheta,
        cy + rx * sinPhi * cosTheta + ry * cosPhi * sinTheta,
    };
}

[[nodiscard]] SvgPathPoint ArcDerivative(double rx, double ry, double phi, double theta) noexcept {
    const double cosPhi = std::cos(phi);
    const double sinPhi = std::sin(phi);
    const double cosTheta = std::cos(theta);
    const double sinTheta = std::sin(theta);
    return {
        -rx * cosPhi * sinTheta - ry * sinPhi * cosTheta,
        -rx * sinPhi * sinTheta + ry * cosPhi * cosTheta,
    };
}

} // namespace

void SvgPathArcSegment::AddBezierSegment(
    Gdiplus::GraphicsPath& path,
    double cx,
    double cy,
    double rx,
    double ry,
    double phi,
    double theta1,
    double theta2) {
    const double delta = theta2 - theta1;
    const double alpha = (4.0 / 3.0) * std::tan(delta / 4.0);

    const SvgPathPoint p1 = ArcPoint(cx, cy, rx, ry, phi, theta1);
    const SvgPathPoint p2 = ArcPoint(cx, cy, rx, ry, phi, theta2);
    const SvgPathPoint d1 = ArcDerivative(rx, ry, phi, theta1);
    const SvgPathPoint d2 = ArcDerivative(rx, ry, phi, theta2);

    const SvgPathPoint c1{ p1.x + alpha * d1.x, p1.y + alpha * d1.y };
    const SvgPathPoint c2{ p2.x - alpha * d2.x, p2.y - alpha * d2.y };
    path.AddBezier(
        SvgPathGdiplusPoint::From(p1),
        SvgPathGdiplusPoint::From(c1),
        SvgPathGdiplusPoint::From(c2),
        SvgPathGdiplusPoint::From(p2));
}

} // namespace kb::editor

#endif
