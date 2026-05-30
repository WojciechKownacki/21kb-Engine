#include "rendering/SvgPathArcMath.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] double VectorAngle(double ux, double uy, double vx, double vy) noexcept {
    const double denominator = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
    if (denominator == 0.0) {
        return 0.0;
    }

    const double dot = std::clamp((ux * vx + uy * vy) / denominator, -1.0, 1.0);
    const double sign = (ux * vy - uy * vx) < 0.0 ? -1.0 : 1.0;
    return sign * std::acos(dot);
}

} // namespace

std::optional<SvgPathArcCenter> SvgPathArcMath::ToCenterArc(SvgPathArcEndpoint endpoint) noexcept {
    endpoint.rx = std::abs(endpoint.rx);
    endpoint.ry = std::abs(endpoint.ry);
    if (endpoint.rx <= 0.0 || endpoint.ry <= 0.0 || (endpoint.from.x == endpoint.to.x && endpoint.from.y == endpoint.to.y)) {
        return std::nullopt;
    }

    const double phi = endpoint.rotationDegrees * kPi / 180.0;
    const double cosPhi = std::cos(phi);
    const double sinPhi = std::sin(phi);
    const double dx = (endpoint.from.x - endpoint.to.x) * 0.5;
    const double dy = (endpoint.from.y - endpoint.to.y) * 0.5;
    const double x1p = cosPhi * dx + sinPhi * dy;
    const double y1p = -sinPhi * dx + cosPhi * dy;

    const double lambda = (x1p * x1p) / (endpoint.rx * endpoint.rx) + (y1p * y1p) / (endpoint.ry * endpoint.ry);
    if (lambda > 1.0) {
        const double scale = std::sqrt(lambda);
        endpoint.rx *= scale;
        endpoint.ry *= scale;
    }

    const double rx2 = endpoint.rx * endpoint.rx;
    const double ry2 = endpoint.ry * endpoint.ry;
    const double x1p2 = x1p * x1p;
    const double y1p2 = y1p * y1p;
    const double numerator = (std::max)(0.0, rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2);
    const double denominator = rx2 * y1p2 + ry2 * x1p2;
    const double sign = (endpoint.largeArc == endpoint.sweep) ? -1.0 : 1.0;
    const double factor = denominator == 0.0 ? 0.0 : sign * std::sqrt(numerator / denominator);
    const double cxp = factor * (endpoint.rx * y1p / endpoint.ry);
    const double cyp = factor * (-endpoint.ry * x1p / endpoint.rx);

    const double cx = cosPhi * cxp - sinPhi * cyp + (endpoint.from.x + endpoint.to.x) * 0.5;
    const double cy = sinPhi * cxp + cosPhi * cyp + (endpoint.from.y + endpoint.to.y) * 0.5;

    const double theta1 = VectorAngle(1.0, 0.0, (x1p - cxp) / endpoint.rx, (y1p - cyp) / endpoint.ry);
    double delta = VectorAngle((x1p - cxp) / endpoint.rx, (y1p - cyp) / endpoint.ry, (-x1p - cxp) / endpoint.rx, (-y1p - cyp) / endpoint.ry);
    if (!endpoint.sweep && delta > 0.0) {
        delta -= 2.0 * kPi;
    } else if (endpoint.sweep && delta < 0.0) {
        delta += 2.0 * kPi;
    }

    return SvgPathArcCenter{
        .cx = cx,
        .cy = cy,
        .rx = endpoint.rx,
        .ry = endpoint.ry,
        .phi = phi,
        .theta1 = theta1,
        .delta = delta,
    };
}

} // namespace kb::editor
