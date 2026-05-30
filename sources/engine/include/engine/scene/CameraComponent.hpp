#pragma once

namespace kb::scene {

enum class CameraProjection {
    Perspective,
    Orthographic
};

struct CameraComponent {
    CameraProjection projection = CameraProjection::Perspective;
    float verticalFovDegrees = 60.0F;
    float orthographicHeight = 10.0F;
    float nearClip = 0.01F;
    float farClip = 1000.0F;
    bool primary = false;
};

} // namespace kb::scene
