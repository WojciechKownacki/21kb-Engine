#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// A shape of the 2D physics: a box, a circle or a capsule, solid or a trigger. It belongs to the Body 2D on its object or the nearest
// parent that has one; with none, it is a static shape.
struct Collider2DComponent {
    static constexpr std::string_view StableId = "kb21.scene.collider2d";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::int32_t shape = 0;
    float width = 1.0F;
    float height = 1.0F;
    float radius = 0.5F;
    float offsetX = 0.0F;
    float offsetY = 0.0F;
    float angleDegrees = 0.0F;
    bool isTrigger = false;
    float density = 1.0F;
    float friction = 0.6F;
    float restitution = 0.0F;
    std::int32_t layer = 0;
    std::uint32_t collidesWith = 65535U;
    bool sideView = true;
    bool enabled = true;
    bool operator==(const Collider2DComponent&) const = default;
};

[[nodiscard]] inline bool IsCollider2DComponentValid(const Collider2DComponent& value) noexcept {
    const auto finite = [](float number, float low, float high) noexcept { return std::isfinite(number) && number >= low && number <= high; };
    return value.shape >= 0 && value.shape <= 2 && finite(value.width, 0.0001F, 100000.0F) && finite(value.height, 0.0001F, 100000.0F) &&
        finite(value.radius, 0.0001F, 100000.0F) && finite(value.offsetX, -100000.0F, 100000.0F) && finite(value.offsetY, -100000.0F, 100000.0F) &&
        finite(value.angleDegrees, -100000.0F, 100000.0F) && finite(value.density, 0.0F, 1000000.0F) && finite(value.friction, 0.0F, 100.0F) &&
        finite(value.restitution, 0.0F, 1.0F) && value.layer >= 0 && value.layer <= 15 && value.collidesWith <= 65535U;
}

} // namespace kb::scene
