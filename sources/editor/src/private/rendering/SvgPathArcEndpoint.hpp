#pragma once

#include "rendering/SvgPathPoint.hpp"

namespace kb::editor {

struct SvgPathArcEndpoint {
    SvgPathPoint from{};
    SvgPathPoint to{};
    double rx = 0.0;
    double ry = 0.0;
    double rotationDegrees = 0.0;
    bool largeArc = false;
    bool sweep = false;
};

} // namespace kb::editor
