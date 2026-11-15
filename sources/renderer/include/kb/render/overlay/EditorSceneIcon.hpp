#pragma once

#include <array>
#include <cstdint>

namespace kb::render {

// What a Scene View icon stands for. Entities that draw their own geometry carry no icon.
enum class EditorSceneIconKind : std::uint8_t {
    ParticleEffect,
    Camera,
    AudioSource,
    Entity,
    Prefab,
};

struct EditorSceneIconDesc {
    EditorSceneIconKind kind = EditorSceneIconKind::Entity;
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 3> iconRight{1.0F, 0.0F, 0.0F};
    std::array<float, 3> iconUp{0.0F, 1.0F, 0.0F};
    float iconWorldScale = 1.0F;
    bool selected = false;
};

} // namespace kb::render
