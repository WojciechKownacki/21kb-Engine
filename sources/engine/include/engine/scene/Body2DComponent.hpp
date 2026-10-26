#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// A body of the 2D physics (Box2D): what moves, and how. A Collider 2D on the same object, or on its children, gives it a shape.
// The plane is the X and Y plane (side view, gravity down) or the X and Z plane (top down).
struct Body2DComponent {
    static constexpr std::string_view StableId = "kb21.scene.body2d";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::int32_t type = 2;
    float gravityScale = 1.0F;
    float linearDamping = 0.0F;
    float angularDamping = 0.0F;
    bool fixedRotation = false;
    bool bullet = false;
    bool allowSleep = true;
    bool sideView = true;
    float startVelocityX = 0.0F;
    float startVelocityY = 0.0F;
    bool enabled = true;
    bool operator==(const Body2DComponent&) const = default;
};

[[nodiscard]] inline bool IsBody2DComponentValid(const Body2DComponent& value) noexcept {
    const auto finite = [](float number, float low, float high) noexcept { return std::isfinite(number) && number >= low && number <= high; };
    return value.type >= 0 && value.type <= 2 && finite(value.gravityScale, -100.0F, 100.0F) && finite(value.linearDamping, 0.0F, 1000.0F) &&
        finite(value.angularDamping, 0.0F, 1000.0F) && finite(value.startVelocityX, -10000.0F, 10000.0F) && finite(value.startVelocityY, -10000.0F, 10000.0F);
}

} // namespace kb::scene
