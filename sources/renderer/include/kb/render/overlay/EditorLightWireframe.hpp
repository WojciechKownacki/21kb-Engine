#pragma once

#include <array>
#include <cstdint>

namespace kb::render {

enum class EditorLightWireframeKind : std::uint8_t {
    Point,
    Spot,
    Directional,
};

struct EditorLightWireframeDesc {
    EditorLightWireframeKind kind = EditorLightWireframeKind::Point;
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 3> forward{0.0F, 0.0F, 1.0F};
    std::array<float, 3> right{1.0F, 0.0F, 0.0F};
    std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    std::array<float, 3> iconRight{1.0F, 0.0F, 0.0F};
    std::array<float, 3> iconUp{0.0F, 1.0F, 0.0F};
    std::array<float, 3> color{1.0F, 0.86F, 0.32F};
    float range = 1.0F;
    float outerConeDegrees = 35.0F;
    float iconWorldScale = 1.0F;
    bool selected = false;
};

} // namespace kb::render
