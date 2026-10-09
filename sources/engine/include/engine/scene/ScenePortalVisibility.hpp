#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::scene {

class Scene;

// The camera a portal visibility pass looks through.
struct ScenePortalCamera {
    kb::math::Vec3 position{};
    // Column-major world-to-clip transform, clip = viewProjection * (x, y, z, 1). Points in front of the
    // camera have w > 0; x/w and y/w span -1..1 across the view.
    std::array<float, 16> viewProjection{};
    // Only cells whose membership mask shares a bit with this one take part.
    std::uint32_t membershipMask = 0xFFFFFFFFU;
    // The world position `position` and `viewProjection` are relative to (a render origin near the camera,
    // docs/large_worlds.md). The cells are placed relative to it too, and Hides takes points relative to it.
    kb::math::DVec3 origin{};
};

// One VisibilityCell taking part in a pass, with the world placement of its RegionShape.
struct ScenePortalVisibilityCell {
    SceneEntity cell{};
    RegionShapeComponent shape{};
    kb::math::Vec3 position{};
    kb::math::Quat rotation{};
    kb::math::Vec3 scale{ 1.0F, 1.0F, 1.0F };
    std::uint32_t membershipMask = 0xFFFFFFFFU;
    VisibilityCellMembership membership = VisibilityCellMembership::Include;
    bool visible = false;
};

// Which visibility cells the camera can see. The camera's cell is visible; from it the pass follows every
// enabled Visibility portal (SceneRegionPortalComponent, source cell to target cell) whose opening is
// on screen, narrowing the visible screen rectangle to the opening at each step, so a cell is visible
// only when some chain of openings leads to it through the view. ForceVisible cells are always visible,
// ForceHidden cells never are and are not looked through. Exclude cells cut their volume out of the
// cells around them. With the camera in no cell, nothing is culled.
class ScenePortalVisibility final {
public:
    [[nodiscard]] bool Active() const noexcept { return active_; }
    [[nodiscard]] std::span<const SceneEntity> CameraCells() const noexcept { return cameraCells_; }
    [[nodiscard]] std::span<const ScenePortalVisibilityCell> Cells() const noexcept { return cells_; }
    [[nodiscard]] bool IsCellVisible(SceneEntity cell) const noexcept;
    // Whether something at `worldPoint` (an entity's bounds centre) is culled: it lies in at least one cell
    // that applies to `membershipMask`, none of those is visible, and it is not in an Exclude cell.
    [[nodiscard]] bool Hides(kb::math::Vec3 worldPoint, std::uint32_t membershipMask = 0xFFFFFFFFU) const noexcept;

private:
    friend ScenePortalVisibility ComputeScenePortalVisibility(const Scene& scene, const ScenePortalCamera& camera);

    bool active_ = false;
    std::vector<SceneEntity> cameraCells_;
    std::vector<ScenePortalVisibilityCell> cells_;
};

// Whether the scene has any enabled VisibilityCell; a renderer skips the pass for scenes without one.
[[nodiscard]] bool SceneHasVisibilityCells(const Scene& scene) noexcept;
[[nodiscard]] ScenePortalVisibility ComputeScenePortalVisibility(const Scene& scene, const ScenePortalCamera& camera);

} // namespace kb::scene
