#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

// A Region Shape is deliberately independent from ColliderComponent. It is
// authoring/runtime geometry for gameplay zones, influences and tools; it
// never creates a physics body or participates in collision solving.
enum class RegionShapeKind : std::uint8_t {
    Circle2D = 0U,
    Rectangle2D = 1U,
    Sphere = 2U,
    Box = 3U,
    Capsule = 4U,
};

struct RegionShapeComponent {
    static constexpr std::string_view StableId = "kb21.scene.region-form";
    static constexpr std::uint32_t SchemaVersion = 1U;

    RegionShapeKind kind = RegionShapeKind::Box;
    Vec3 center{};
    // Rectangle2D uses x/y; Box uses all axes. Values are full extents.
    Vec3 size{ 1.0F, 1.0F, 1.0F };
    float radius = 0.5F;
    // Capsule height includes its two hemispherical caps and uses local +Y.
    float height = 2.0F;
    bool enabled = true;
};

[[nodiscard]] constexpr bool IsRegionShapeKindValid(RegionShapeKind kind) noexcept {
    return kind == RegionShapeKind::Circle2D || kind == RegionShapeKind::Rectangle2D ||
        kind == RegionShapeKind::Sphere || kind == RegionShapeKind::Box ||
        kind == RegionShapeKind::Capsule;
}

} // namespace kb::scene
