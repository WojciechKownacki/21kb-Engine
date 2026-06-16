#pragma once

#include <cstdint>

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
    Vec3 worldPosition{};
    Quat worldRotation{};
    Vec3 worldScale{ 1.0F, 1.0F, 1.0F };
    std::uint64_t localVersion = 1;
    std::uint64_t parentVersion = 0;
    std::uint64_t worldVersion = 0;
    bool worldDirty = true;
};

} // namespace kb::scene
