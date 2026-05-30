#pragma once

namespace kb::scene {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Quat {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

struct TransformComponent {
    Vec3 localPosition{};
    Quat localRotation{};
    Vec3 localScale{ 1.0F, 1.0F, 1.0F };
};

} // namespace kb::scene
