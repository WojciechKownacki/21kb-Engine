#pragma once

#include <array>

namespace kb::render {

struct EditorParticleIconDesc {
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 3> iconRight{1.0F, 0.0F, 0.0F};
    std::array<float, 3> iconUp{0.0F, 1.0F, 0.0F};
    float iconWorldScale = 1.0F;
    bool selected = false;
};

} // namespace kb::render
