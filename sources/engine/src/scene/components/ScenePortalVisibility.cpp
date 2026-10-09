#include "engine/scene/ScenePortalVisibility.hpp"

#include "engine/scene/RegionPortalComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneRegionShapeQueries.hpp"
#include "engine/scene/SceneRegionShapeComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityCounter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace kb::scene {
namespace {

using kb::math::Vec3;

// Openings are followed at most this many cells deep from the camera's cell, and at most this many
// openings are looked through in one pass; a portal graph too large for that culls nothing.
constexpr std::size_t kMaximumPortalDepth = 64U;
constexpr std::size_t kMaximumPortalTraversals = 4096U;
constexpr float kNearW = 1.0e-4F;

struct ScreenRect {
    float minX = -1.0F;
    float minY = -1.0F;
    float maxX = 1.0F;
    float maxY = 1.0F;

    [[nodiscard]] bool Empty() const noexcept { return minX >= maxX || minY >= maxY; }
    [[nodiscard]] ScreenRect Intersect(const ScreenRect& other) const noexcept {
        return ScreenRect{ std::max(minX, other.minX), std::max(minY, other.minY), std::min(maxX, other.maxX), std::min(maxY, other.maxY) };
    }
};

struct ClipPoint {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
};

struct Portal {
    SceneEntity entity{};
    std::size_t source = 0U;
    std::size_t target = 0U;
    ScenePortalVisibilityCell volume{};
};

[[nodiscard]] ClipPoint ToClip(const std::array<float, 16>& matrix, Vec3 point) noexcept {
    return ClipPoint{
        matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
        matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
        matrix[3] * point.x + matrix[7] * point.y + matrix[11] * point.z + matrix[15],
    };
}

[[nodiscard]] bool Contains(const ScenePortalVisibilityCell& cell, Vec3 worldPoint) noexcept {
    if (std::fabs(cell.scale.x) <= 1.0e-6F || std::fabs(cell.scale.y) <= 1.0e-6F || std::fabs(cell.scale.z) <= 1.0e-6F) return false;
    Vec3 local = kb::math::Rotate(kb::math::Inverse(cell.rotation), worldPoint - cell.position);
    local.x /= cell.scale.x;
    local.y /= cell.scale.y;
    local.z /= cell.scale.z;
    return RegionShapeContainsLocal(cell.shape, local);
}

// Corners of a box holding the shape, in world space. 2D shapes lie in their local XY plane.
[[nodiscard]] std::vector<Vec3> BoundingCorners(const ScenePortalVisibilityCell& volume) {
    const RegionShapeComponent& shape = volume.shape;
    Vec3 half{};
    switch (shape.kind) {
    case RegionShapeKind::Circle2D: half = Vec3{ shape.radius, shape.radius, 0.0F }; break;
    case RegionShapeKind::Rectangle2D: half = Vec3{ shape.size.x * 0.5F, shape.size.y * 0.5F, 0.0F }; break;
    case RegionShapeKind::Sphere: half = Vec3{ shape.radius, shape.radius, shape.radius }; break;
    case RegionShapeKind::Box: half = shape.size * 0.5F; break;
    case RegionShapeKind::Capsule: half = Vec3{ shape.radius, shape.height * 0.5F, shape.radius }; break;
    }
    std::vector<Vec3> corners;
    corners.reserve(8U);
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 local{
            shape.center.x + ((corner & 1) != 0 ? half.x : -half.x),
            shape.center.y + ((corner & 2) != 0 ? half.y : -half.y),
            shape.center.z + ((corner & 4) != 0 ? half.z : -half.z),
        };
        const Vec3 scaled{ local.x * volume.scale.x, local.y * volume.scale.y, local.z * volume.scale.z };
        corners.push_back(volume.position + kb::math::Rotate(volume.rotation, scaled));
    }
    return corners;
}

// The screen rectangle the portal's opening covers, clipped to the near plane; nothing when the opening is
// wholly behind the camera.
[[nodiscard]] std::optional<ScreenRect> ProjectedRect(const ScenePortalCamera& camera, const ScenePortalVisibilityCell& volume) {
    if (Contains(volume, camera.position)) return ScreenRect{};
    const std::vector<Vec3> corners = BoundingCorners(volume);
    std::vector<ClipPoint> clip;
    clip.reserve(corners.size());
    for (const Vec3& corner : corners) clip.push_back(ToClip(camera.viewProjection, corner));
    ScreenRect rect{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
    bool any = false;
    const auto include = [&rect, &any](const ClipPoint& point) {
        const float x = point.x / point.w;
        const float y = point.y / point.w;
        rect.minX = std::min(rect.minX, x);
        rect.minY = std::min(rect.minY, y);
        rect.maxX = std::max(rect.maxX, x);
        rect.maxY = std::max(rect.maxY, y);
        any = true;
    };
    for (std::size_t first = 0U; first < clip.size(); ++first) {
        if (clip[first].w > kNearW) include(clip[first]);
        // Where the hull of the corners crosses the near plane: every segment between two corners lies in
        // the hull, and the hull's near-plane section has its vertices on such segments.
        for (std::size_t second = first + 1U; second < clip.size(); ++second) {
            const ClipPoint& a = clip[first];
            const ClipPoint& b = clip[second];
            if ((a.w > kNearW) == (b.w > kNearW)) continue;
            const float t = (a.w - kNearW) / (a.w - b.w);
            include(ClipPoint{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, kNearW });
        }
    }
    if (!any) return std::nullopt;
    return rect;
}

[[nodiscard]] bool ReadVolume(const Scene& scene, SceneEntity entity, ScenePortalVisibilityCell& volume, const kb::math::DVec3& origin) {
    const RegionShapeComponent* shape = scene.Components().RegionShapes().TryGet(entity);
    const TransformComponent* transform = scene.Transforms().TryGet(entity);
    if (shape == nullptr || transform == nullptr || !shape->enabled || !IsRegionShapeKindValid(shape->kind)) return false;
    volume.cell = entity;
    volume.shape = *shape;
    volume.position = kb::math::RelativeTo(scene.Transforms().WorldTranslation(entity, *transform), origin);
    volume.rotation = transform->worldRotation;
    volume.scale = transform->worldScale;
    return true;
}

} // namespace

bool ScenePortalVisibility::IsCellVisible(SceneEntity cell) const noexcept {
    const auto found = std::ranges::find(cells_, cell, &ScenePortalVisibilityCell::cell);
    return found != cells_.end() && found->visible;
}

bool ScenePortalVisibility::Hides(kb::math::Vec3 worldPoint, std::uint32_t membershipMask) const noexcept {
    if (!active_) return false;
    bool contained = false;
    for (const ScenePortalVisibilityCell& cell : cells_) {
        if ((cell.membershipMask & membershipMask) == 0U || !Contains(cell, worldPoint)) continue;
        if (cell.membership == VisibilityCellMembership::Exclude || cell.visible) return false;
        contained = true;
    }
    return contained;
}

bool SceneHasVisibilityCells(const Scene& scene) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    return SceneEntityCounter::CountWithComponent(state.world, state.components.VisibilityCellComponentId()) != 0U;
}

ScenePortalVisibility ComputeScenePortalVisibility(const Scene& scene, const ScenePortalCamera& camera) {
    ScenePortalVisibility result;
    SceneState& state = SceneAccess::State(const_cast<Scene&>(scene));
    std::vector<std::pair<SceneEntity, VisibilityCellComponent>> authoredCells;
    state.world.CreateQuery<VisibilityCellComponent>().ForEach(
        [](SceneEntity entity, const VisibilityCellComponent& cell, void* context) {
            static_cast<std::vector<std::pair<SceneEntity, VisibilityCellComponent>>*>(context)->emplace_back(entity, cell);
        }, &authoredCells);
    std::ranges::sort(authoredCells, {}, [](const auto& entry) { return entry.first.Id(); });

    std::vector<VisibilityCellOverride> overrides;
    for (const auto& [entity, cell] : authoredCells) {
        if (!cell.enabled || !IsVisibilityCellComponentValid(cell) || (cell.membershipMask & camera.membershipMask) == 0U) continue;
        ScenePortalVisibilityCell volume;
        if (!ReadVolume(scene, entity, volume, camera.origin)) continue;
        volume.membershipMask = cell.membershipMask;
        volume.membership = cell.membership;
        result.cells_.push_back(volume);
        overrides.push_back(cell.visibilityOverride);
    }
    const auto indexOf = [&result](SceneEntity entity) -> std::optional<std::size_t> {
        for (std::size_t index = 0U; index < result.cells_.size(); ++index) {
            if (result.cells_[index].cell == entity) return index;
        }
        return std::nullopt;
    };

    // The camera's cells: every Include cell around it, unless it stands in an Exclude cell.
    std::vector<std::size_t> starts;
    for (std::size_t index = 0U; index < result.cells_.size(); ++index) {
        if (!Contains(result.cells_[index], camera.position)) continue;
        if (result.cells_[index].membership == VisibilityCellMembership::Exclude) {
            starts.clear();
            break;
        }
        starts.push_back(index);
    }
    for (std::size_t index = 0U; index < result.cells_.size(); ++index) {
        if (overrides[index] == VisibilityCellOverride::ForceVisible) result.cells_[index].visible = true;
    }
    if (starts.empty()) {
        for (ScenePortalVisibilityCell& cell : result.cells_) cell.visible = true;
        return result;
    }
    result.active_ = true;

    std::vector<std::pair<SceneEntity, SceneRegionPortalComponent>> authoredPortals;
    state.world.CreateQuery<SceneRegionPortalComponent>().ForEach(
        [](SceneEntity entity, const SceneRegionPortalComponent& portal, void* context) {
            static_cast<std::vector<std::pair<SceneEntity, SceneRegionPortalComponent>>*>(context)->emplace_back(entity, portal);
        }, &authoredPortals);
    std::ranges::sort(authoredPortals, {}, [](const auto& entry) { return entry.first.Id(); });
    std::vector<Portal> portals;
    for (const auto& [entity, portal] : authoredPortals) {
        if (!portal.enabled || !IsSceneRegionPortalComponentValid(portal) ||
            (portal.purposes & static_cast<RegionPortalPurposeMask>(RegionPortalPurpose::Visibility)) == 0U) continue;
        const std::optional<std::size_t> source = indexOf(portal.sourceCell);
        const std::optional<std::size_t> target = indexOf(portal.targetCell);
        if (!source.has_value() || !target.has_value()) continue;
        Portal entry{ .entity = entity, .source = *source, .target = *target };
        if (!ReadVolume(scene, entity, entry.volume, camera.origin)) continue;
        portals.push_back(entry);
    }

    std::vector<bool> onPath(result.cells_.size(), false);
    std::size_t traversals = 0U;
    // Depth-first through the openings, each step narrowed to what the previous openings let through.
    const auto visit = [&](const auto& self, std::size_t cell, const ScreenRect& view, std::size_t depth) -> void {
        onPath[cell] = true;
        for (const Portal& portal : portals) {
            if (portal.source != cell || onPath[portal.target] || overrides[portal.target] == VisibilityCellOverride::ForceHidden) continue;
            if (++traversals > kMaximumPortalTraversals) break;
            const std::optional<ScreenRect> opening = ProjectedRect(camera, portal.volume);
            if (!opening.has_value()) continue;
            const ScreenRect through = view.Intersect(*opening);
            if (through.Empty()) continue;
            result.cells_[portal.target].visible = true;
            if (depth + 1U < kMaximumPortalDepth) self(self, portal.target, through, depth + 1U);
        }
        onPath[cell] = false;
    };
    for (const std::size_t start : starts) {
        if (overrides[start] == VisibilityCellOverride::ForceHidden) continue;
        result.cells_[start].visible = true;
        result.cameraCells_.push_back(result.cells_[start].cell);
        visit(visit, start, ScreenRect{}, 0U);
    }
    if (traversals > kMaximumPortalTraversals) {
        for (ScenePortalVisibilityCell& cell : result.cells_) cell.visible = true;
    }
    for (std::size_t index = 0U; index < result.cells_.size(); ++index) {
        if (overrides[index] == VisibilityCellOverride::ForceHidden) result.cells_[index].visible = false;
    }
    return result;
}

} // namespace kb::scene
