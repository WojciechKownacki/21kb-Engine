#include "app/scene_viewport/EditorTerrainViewportInteraction.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] std::optional<kb::scene::Vec3> TerrainPlaneHit(
    const EditorSceneViewportRay& ray,
    const kb::scene::TransformComponent& transform) noexcept {
    const kb::scene::Vec3 normal = kb::math::Rotate(transform.worldRotation, kb::scene::Vec3{ 0.0F, 1.0F, 0.0F });
    const float denominator = kb::math::Dot(normal, ray.direction);
    if (std::abs(denominator) <= 0.00001F) return std::nullopt;
    const float distance = kb::math::Dot(normal, transform.worldPosition - ray.origin) / denominator;
    if (distance <= 0.05F) return std::nullopt;
    return ray.origin + ray.direction * distance;
}

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

} // namespace

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
    const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (!tool.editingEnabled || !EditorTerrainService::IsTerrainEntity(sceneContext.Scene(), entity)) return false;
    const std::optional<EditorSceneViewportHit> rayHit = EditorSceneViewportHitResolver::ResolveRay(
        sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!rayHit.has_value()) return false;
    const kb::scene::TransformComponent transform = sceneContext.Scene().Transforms().Get(entity);
    const std::optional<kb::scene::Vec3> world = TerrainPlaneHit(rayHit->ray, transform);
    const std::optional<kb::scene::Vec3> local = world.has_value() ? ToTerrainLocal(*world, transform) : std::nullopt;
    const std::optional<kb::assets::TerrainAsset> terrain = EditorTerrainService::Load(sceneContext.Scene(), entity);
    if (!local.has_value() || !terrain.has_value() || std::abs(local->x) > terrain->worldSizeX * 0.5F || std::abs(local->z) > terrain->worldSizeZ * 0.5F) return false;
    if (!beginStroke) {
        const float dx = local->x - tool.lastStampX;
        const float dz = local->z - tool.lastStampZ;
        const float minimumSpacing = std::max(terrain->worldSizeX / static_cast<float>(terrain->width - 1U), tool.brush.radius * 0.12F);
        if (dx * dx + dz * dz < minimumSpacing * minimumSpacing) return true;
    }
    std::string error;
    if (!EditorTerrainService::ApplyBrush(
            sceneContext.Scene(), entity, tool.brush,
            kb::terrain_editor::TerrainBrushStamp{ .localX = local->x, .localZ = local->z }, &error)) {
        sceneContext.Console().Warning("Terrain", error.empty() ? "Terrain brush stamp failed." : error);
        return true;
    }
    tool.strokeActive = true;
    tool.lastStampX = local->x;
    tool.lastStampZ = local->z;
    return true;
}

} // namespace kb::editor
#endif
