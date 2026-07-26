#pragma once

#include <array>
#include <cstdint>

namespace kb::render {

enum class EditorCameraWireframeProjection : std::uint8_t {
    Perspective,
    Orthographic,
};

struct EditorCameraWireframeDesc {
    EditorCameraWireframeProjection projection = EditorCameraWireframeProjection::Perspective;
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 3> forward{0.0F, 0.0F, 1.0F};
    std::array<float, 3> right{1.0F, 0.0F, 0.0F};
    std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    float verticalFovDegrees = 60.0F;
    float orthographicHeight = 10.0F;
    float nearClip = 0.01F;
    float farClip = 1000.0F;
    float aspect = 1.0F;
};

} // namespace kb::render
