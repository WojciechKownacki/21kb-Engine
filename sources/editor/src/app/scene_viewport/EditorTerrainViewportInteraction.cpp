#include "app/scene_viewport/EditorTerrainViewportInteraction.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace kb::editor {
namespace {

struct ResolvedTerrainPointer {
    kb::scene::SceneEntity entity{};
    kb::scene::Vec3 local{};
    float minimumSampleSpacing = 0.0F;
};

struct TerrainPickContext {
    EditorSceneContext* sceneContext = nullptr;
    const EditorSceneViewportRay* ray = nullptr;
    kb::scene::SceneEntity entity{};
    kb::scene::SceneEntity ignoredEntity{};
    float distance = std::numeric_limits<float>::max();
};

[[nodiscard]] std::optional<kb::scene::Vec3> ToTerrainLocal(
    kb::scene::Vec3 world,
    const kb::scene::TransformComponent& transform) noexcept {
    if (std::abs(transform.worldScale.x) <= 0.00001F || std::abs(transform.worldScale.y) <= 0.00001F ||
        std::abs(transform.worldScale.z) <= 0.00001F) return std::nullopt;
    const kb::scene::Vec3 unrotated = kb::math::Rotate(kb::math::Inverse(transform.worldRotation), world - transform.worldPosition);
    return kb::scene::Vec3{
        unrotated.x / transform.worldScale.x,
        unrotated.y / transform.worldScale.y,
        unrotated.z / transform.worldScale.z,
    };
}

[[nodiscard]] float HeightAt(
    const kb::assets::TerrainAsset& terrain,
    float localX,
    float localZ) noexcept {
    const float sampleX = std::clamp(
        (localX + terrain.worldSizeX * 0.5F) / terrain.worldSizeX,
        0.0F, 1.0F) * static_cast<float>(terrain.width - 1U);
    const float sampleZ = std::clamp(
        (localZ + terrain.worldSizeZ * 0.5F) / terrain.worldSizeZ,
        0.0F, 1.0F) * static_cast<float>(terrain.height - 1U);
    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(sampleX));
    const std::uint32_t z0 = static_cast<std::uint32_t>(std::floor(sampleZ));
    const std::uint32_t x1 = std::min(x0 + 1U, terrain.width - 1U);
    const std::uint32_t z1 = std::min(z0 + 1U, terrain.height - 1U);
    const auto at = [&terrain](std::uint32_t x, std::uint32_t z) {
        return terrain.heights[static_cast<std::size_t>(z) * terrain.width + x];
    };
    const float top = std::lerp(at(x0, z0), at(x1, z0), sampleX - static_cast<float>(x0));
    const float bottom = std::lerp(at(x0, z1), at(x1, z1), sampleX - static_cast<float>(x0));
    return std::lerp(top, bottom, sampleZ - static_cast<float>(z0));
}

[[nodiscard]] bool ClipRayAxis(
    float origin,
    float direction,
    float minimum,
    float maximum,
    float& enter,
    float& exit) noexcept {
    if (std::abs(direction) <= 0.000001F) {
        return origin >= minimum && origin <= maximum;
    }
    float first = (minimum - origin) / direction;
    float second = (maximum - origin) / direction;
    if (first > second) std::swap(first, second);
    enter = std::max(enter, first);
    exit = std::min(exit, second);
    return enter <= exit;
}

[[nodiscard]] std::optional<kb::scene::Vec3> TerrainSurfaceHit(
    const EditorSceneViewportRay& ray,
    const kb::scene::TransformComponent& transform,
    const kb::assets::TerrainAsset& terrain) noexcept {
    const std::optional<kb::scene::Vec3> origin = ToTerrainLocal(ray.origin, transform);
    const std::optional<kb::scene::Vec3> next = ToTerrainLocal(ray.origin + ray.direction, transform);
    if (!origin.has_value() || !next.has_value()) return std::nullopt;
    const kb::scene::Vec3 direction = *next - *origin;
    const auto [minimumHeight, maximumHeight] = std::ranges::minmax_element(terrain.heights);
    float enter = 0.05F;
    float exit = std::numeric_limits<float>::max();
    if (!ClipRayAxis(origin->x, direction.x, -terrain.worldSizeX * 0.5F, terrain.worldSizeX * 0.5F, enter, exit) ||
        !ClipRayAxis(origin->z, direction.z, -terrain.worldSizeZ * 0.5F, terrain.worldSizeZ * 0.5F, enter, exit) ||
        !ClipRayAxis(origin->y, direction.y, *minimumHeight - 0.1F, *maximumHeight + 0.1F, enter, exit) ||
        !std::isfinite(enter) || !std::isfinite(exit)) {
        return std::nullopt;
    }
    const auto position = [&](float distance) { return *origin + direction * distance; };
    const auto difference = [&](float distance) {
        const kb::scene::Vec3 point = position(distance);
        return point.y - HeightAt(terrain, point.x, point.z);
    };
    float previousDistance = enter;
    float previousDifference = difference(previousDistance);
    if (std::abs(previousDifference) <= 0.001F) return position(previousDistance);

    const float cellX = terrain.worldSizeX / static_cast<float>(terrain.width - 1U);
    const float cellZ = terrain.worldSizeZ / static_cast<float>(terrain.height - 1U);
    const float cellsPerDistance = std::hypot(direction.x / cellX, direction.z / cellZ);
    const float interval = exit - enter;
    const float step = cellsPerDistance > 0.00001F
        ? std::clamp(0.5F / cellsPerDistance, interval / 1024.0F, interval)
        : interval;
    for (float distance = std::min(enter + step, exit); distance <= exit + 0.00001F;) {
        const float currentDifference = difference(distance);
        if ((previousDifference >= 0.0F && currentDifference <= 0.0F) ||
            (previousDifference <= 0.0F && currentDifference >= 0.0F)) {
            float low = previousDistance;
            float high = distance;
            for (int iteration = 0; iteration < 12; ++iteration) {
                const float middle = (low + high) * 0.5F;
                const float middleDifference = difference(middle);
                if ((previousDifference >= 0.0F && middleDifference >= 0.0F) ||
                    (previousDifference <= 0.0F && middleDifference <= 0.0F)) {
                    low = middle;
                } else {
                    high = middle;
                }
            }
            return position((low + high) * 0.5F);
        }
        if (distance >= exit) break;
        previousDistance = distance;
        previousDifference = currentDifference;
        distance = std::min(distance + step, exit);
    }
    return std::nullopt;
}

[[nodiscard]] kb::scene::Vec3 ToWorld(
    kb::scene::Vec3 local,
    const kb::scene::TransformComponent& transform) noexcept {
    const kb::scene::Vec3 scaled{
        local.x * transform.worldScale.x,
        local.y * transform.worldScale.y,
        local.z * transform.worldScale.z,
    };
    return transform.worldPosition + kb::math::Rotate(transform.worldRotation, scaled);
}

void ConsiderTerrainPick(
    kb::scene::SceneEntity entity,
    const kb::scene::TransformComponent& transform,
    const kb::scene::MeshRendererComponent& renderer,
    void* opaque) {
    static_cast<void>(renderer);
    auto& pick = *static_cast<TerrainPickContext*>(opaque);
    if (entity == pick.ignoredEntity || pick.sceneContext == nullptr || pick.ray == nullptr ||
        !EditorTerrainService::IsTerrainEntity(pick.sceneContext->Scene(), entity)) return;
    std::optional<kb::assets::TerrainAsset> loaded;
    const kb::assets::TerrainAsset* terrain = nullptr;
    if (pick.sceneContext->SelectedEntity() == entity) {
        terrain = pick.sceneContext->TerrainForEditing(entity);
    } else {
        loaded = EditorTerrainService::Load(pick.sceneContext->Scene(), entity);
        terrain = loaded.has_value() ? &*loaded : nullptr;
    }
    if (terrain == nullptr) return;
    const std::optional<kb::scene::Vec3> local = TerrainSurfaceHit(*pick.ray, transform, *terrain);
    if (!local.has_value()) return;
    const float distance = EditorSceneViewportMath::Dot(
        ToWorld(*local, transform) - pick.ray->origin,
        pick.ray->direction);
    if (distance > 0.0F && distance < pick.distance) {
        pick.entity = entity;
        pick.distance = distance;
    }
}

[[nodiscard]] std::optional<ResolvedTerrainPointer> ResolveTerrainPointer(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (!sceneContext.IsProjectPluginEnabled("Editor.Terrain") ||
        !EditorTerrainService::IsTerrainEntity(sceneContext.Scene(), entity)) {
        return std::nullopt;
    }
    const std::optional<EditorSceneViewportHit> rayHit = EditorSceneViewportHitResolver::ResolveRay(
        sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!rayHit.has_value()) return std::nullopt;
    const kb::assets::TerrainAsset* terrain = sceneContext.TerrainForEditing(entity);
    if (terrain == nullptr) return std::nullopt;
    const kb::scene::TransformComponent transform = sceneContext.Scene().Transforms().Get(entity);
    const std::optional<kb::scene::Vec3> local = TerrainSurfaceHit(rayHit->ray, transform, *terrain);
    if (!local.has_value() ||
        std::abs(local->x) > terrain->worldSizeX * 0.5F ||
        std::abs(local->z) > terrain->worldSizeZ * 0.5F) {
        return std::nullopt;
    }
    return ResolvedTerrainPointer{
        .entity = entity,
        .local = *local,
        .minimumSampleSpacing = std::max(
            terrain->worldSizeX / static_cast<float>(terrain->width - 1U),
            terrain->worldSizeZ / static_cast<float>(terrain->height - 1U)),
    };
}

[[nodiscard]] bool ClearHover(EditorTerrainToolState& tool) noexcept {
    const bool changed = tool.hoverVisible || tool.hoverEntityId != 0U;
    tool.hoverVisible = false;
    tool.hoverEntityId = 0U;
    return changed;
}

} // namespace

bool EditorTerrainViewportInteraction::SelectAt(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    if (!sceneContext.IsProjectPluginEnabled("Editor.Terrain")) return false;
    const std::optional<EditorSceneViewportHit> rayHit = EditorSceneViewportHitResolver::ResolveRay(
        sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!rayHit.has_value()) return false;

    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (EditorTerrainService::IsTerrainEntity(sceneContext.Scene(), selected)) {
        const kb::assets::TerrainAsset* terrain = sceneContext.TerrainForEditing(selected);
        if (terrain != nullptr) {
            const kb::scene::TransformComponent transform = sceneContext.Scene().Transforms().Get(selected);
            if (TerrainSurfaceHit(rayHit->ray, transform, *terrain).has_value()) {
                return !tool.editingEnabled || tool.mode == EditorTerrainToolMode::Select;
            }
        }
    }

    TerrainPickContext pick{
        .sceneContext = &sceneContext,
        .ray = &rayHit->ray,
        .ignoredEntity = selected,
    };
    sceneContext.Scene().Components().Visitors().ForEachMeshRenderer(&ConsiderTerrainPick, &pick);
    if (!pick.entity.IsValid()) return false;

    if (sceneContext.SelectedEntity() != pick.entity) {
        sceneContext.SelectEntity(pick.entity);
        return true;
    }
    return !tool.editingEnabled || tool.mode == EditorTerrainToolMode::Select;
}

bool EditorTerrainViewportInteraction::UpdateHover(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    if (!tool.editingEnabled || tool.mode == EditorTerrainToolMode::Select) {
        return ClearHover(tool);
    }
    const std::optional<ResolvedTerrainPointer> pointer = ResolveTerrainPointer(
        sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!pointer.has_value()) return ClearHover(tool);
    const bool changed = !tool.hoverVisible || tool.hoverEntityId != pointer->entity.Id() ||
        std::abs(tool.hoverLocalX - pointer->local.x) > 0.001F ||
        std::abs(tool.hoverLocalZ - pointer->local.z) > 0.001F;
    tool.hoverVisible = true;
    tool.hoverEntityId = pointer->entity.Id();
    tool.hoverLocalX = pointer->local.x;
    tool.hoverLocalZ = pointer->local.z;
    return changed;
}

bool EditorTerrainViewportInteraction::Stamp(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    bool beginStroke) {
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    const std::optional<ResolvedTerrainPointer> pointer = ResolveTerrainPointer(
        sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!pointer.has_value()) return false;
    tool.hoverVisible = true;
    tool.hoverEntityId = pointer->entity.Id();
    tool.hoverLocalX = pointer->local.x;
    tool.hoverLocalZ = pointer->local.z;
    if (!tool.editingEnabled || tool.mode == EditorTerrainToolMode::Select) {
        return false;
    }
    if (!beginStroke) {
        const float dx = pointer->local.x - tool.lastStampX;
        const float dz = pointer->local.z - tool.lastStampZ;
        const float minimumSpacing = std::max(
            pointer->minimumSampleSpacing,
            tool.brush.radius * 0.12F);
        if (dx * dx + dz * dz < minimumSpacing * minimumSpacing) return true;
    }
    std::string error;
    if (!sceneContext.ApplyTerrainBrushStamp(
            pointer->entity, tool.brush,
            kb::terrain_editor::TerrainBrushStamp{
                .localX = pointer->local.x,
                .localZ = pointer->local.z },
            beginStroke, &error)) {
        sceneContext.Console().Warning("Terrain", error.empty() ? "Terrain brush stamp failed." : error);
        return true;
    }
    tool.strokeActive = true;
    tool.lastStampX = pointer->local.x;
    tool.lastStampZ = pointer->local.z;
    return true;
}

} // namespace kb::editor
#endif
