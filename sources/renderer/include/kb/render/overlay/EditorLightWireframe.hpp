#pragma once

#include <array>
#include <cstdint>
#include <vector>

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

// One straight piece of a light's range shape. `planeA` and `planeB` span the plane the piece is
// drawn in, so a drawn ribbon can be widened inside it.
struct EditorLightWireframeLine {
    std::array<float, 3> from{0.0F, 0.0F, 0.0F};
    std::array<float, 3> to{0.0F, 0.0F, 0.0F};
    std::array<float, 3> planeA{1.0F, 0.0F, 0.0F};
    std::array<float, 3> planeB{0.0F, 1.0F, 0.0F};
    float alpha = 1.0F;
};

// Appends every line of the light's range shape: three axis circles for a point light, the far
// ring and four edges for a spot light. A directional light, or a light without range, has none.
// The Scene View draws exactly these lines and picks against exactly these lines.
void BuildEditorLightWireframeLines(
    const EditorLightWireframeDesc& light,
    std::vector<EditorLightWireframeLine>& output);

} // namespace kb::render
