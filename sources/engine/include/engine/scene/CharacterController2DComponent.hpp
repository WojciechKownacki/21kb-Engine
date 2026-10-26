#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// A character moved by script input that collides and slides along shapes, climbs slopes up to a limit and steps over small ledges,
// without being pushed by physics (Box2D character mover). Scripts drive it with Physics2D.MoveCharacter and Physics2D.JumpCharacter.
struct CharacterController2DComponent {
    static constexpr std::string_view StableId = "kb21.scene.charactercontroller2d";
    static constexpr std::uint32_t SchemaVersion = 1U;

    float radius = 0.3F;
    float height = 1.0F;
    float gravityScale = 1.0F;
    bool useGravity = true;
    float slopeLimitDegrees = 50.0F;
    float stepHeight = 0.2F;
    std::int32_t layer = 0;
    std::uint32_t collidesWith = 65535U;
    bool sideView = true;
    bool enabled = true;
    bool operator==(const CharacterController2DComponent&) const = default;
};

[[nodiscard]] inline bool IsCharacterController2DComponentValid(const CharacterController2DComponent& value) noexcept {
    const auto finite = [](float number, float low, float high) noexcept { return std::isfinite(number) && number >= low && number <= high; };
    return finite(value.radius, 0.0001F, 1000.0F) && finite(value.height, 0.0002F, 10000.0F) && value.height >= value.radius * 2.0F &&
        finite(value.gravityScale, -100.0F, 100.0F) && finite(value.slopeLimitDegrees, 0.0F, 89.0F) && finite(value.stepHeight, 0.0F, 1000.0F) &&
        value.layer >= 0 && value.layer <= 15 && value.collidesWith <= 65535U;
}

} // namespace kb::scene
