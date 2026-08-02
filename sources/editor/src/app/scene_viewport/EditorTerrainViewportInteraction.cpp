#include "app/scene_viewport/EditorTerrainViewportInteraction.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "app/scene_viewport/EditorTerrainStrokeTickPolicy.hpp"
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

[[nodiscard]] float RayTriangleDistance(
    kb::scene::Vec3 origin,
    kb::scene::Vec3 direction,
    kb::scene::Vec3 a,
    kb::scene::Vec3 b,
    kb::scene::Vec3 c) noexcept {
    constexpr float kEpsilon = 0.000001F;
    const kb::scene::Vec3 edgeAb = b - a;
    const kb::scene::Vec3 edgeAc = c - a;
    const kb::scene::Vec3 perpendicular = kb::math::Cross(direction, edgeAc);
    const float determinant = EditorSceneViewportMath::Dot(edgeAb, perpendicular);
    if (std::abs(determinant) <= kEpsilon) return std::numeric_limits<float>::max();
    const float inverseDeterminant = 1.0F / determinant;
    const kb::scene::Vec3 fromA = origin - a;
    const float u = EditorSceneViewportMath::Dot(fromA, perpendicular) * inverseDeterminant;
    if (u < 0.0F || u > 1.0F) return std::numeric_limits<float>::max();
    const kb::scene::Vec3 cross = kb::math::Cross(fromA, edgeAb);
    const float v = EditorSceneViewportMath::Dot(direction, cross) * inverseDeterminant;
    if (v < 0.0F || u + v > 1.0F) return std::numeric_limits<float>::max();
    const float distance = EditorSceneViewportMath::Dot(edgeAc, cross) * inverseDeterminant;
    return distance > kEpsilon ? distance : std::numeric_limits<float>::max();
}

[[nodiscard]] std::optional<kb::scene::Vec3> TerrainSurfaceHit(
    const EditorSceneViewportRay& ray,
    const kb::scene::TransformComponent& transform,
    const kb::assets::TerrainAsset& terrain) noexcept {
    const std::optional<kb::scene::Vec3> origin = ToTerrainLocal(ray.origin, transform);
    const std::optional<kb::scene::Vec3> next = ToTerrainLocal(ray.origin + ray.direction, transform);
    if (!origin.has_value() || !next.has_value()) return std::nullopt;
    const kb::scene::Vec3 direction = *next - *origin;
    float enter = 0.05F;
    float exit = std::numeric_limits<float>::max();
    if (!ClipRayAxis(origin->x, direction.x, -terrain.worldSizeX * 0.5F, terrain.worldSizeX * 0.5F, enter, exit) ||
        !ClipRayAxis(origin->z, direction.z, -terrain.worldSizeZ * 0.5F, terrain.worldSizeZ * 0.5F, enter, exit) ||
        !std::isfinite(enter)) {
        return std::nullopt;
    }
    const float cellX = terrain.worldSizeX / static_cast<float>(terrain.width - 1U);
    const float cellZ = terrain.worldSizeZ / static_cast<float>(terrain.height - 1U);
    const auto height = [&terrain](std::uint32_t x, std::uint32_t z) noexcept {
        return terrain.heights[static_cast<std::size_t>(z) * terrain.width + x];
    };
    const auto testCell = [&](std::uint32_t x, std::uint32_t z, float segmentEnter, float segmentExit) {
        if (terrain.holes[static_cast<std::size_t>(z) * (terrain.width - 1U) + x] != 0U) {
            return std::numeric_limits<float>::max();
        }
        const float x0 = static_cast<float>(x) * cellX - terrain.worldSizeX * 0.5F;
        const float z0 = static_cast<float>(z) * cellZ - terrain.worldSizeZ * 0.5F;
        const kb::scene::Vec3 a{ x0, height(x, z), z0 };
        const kb::scene::Vec3 b{ x0 + cellX, height(x + 1U, z), z0 };
        const kb::scene::Vec3 c{ x0, height(x, z + 1U), z0 + cellZ };
        const kb::scene::Vec3 d{ x0 + cellX, height(x + 1U, z + 1U), z0 + cellZ };
        const float first = RayTriangleDistance(*origin, direction, a, c, b);
        const float second = RayTriangleDistance(*origin, direction, b, c, d);
        const float nearest = std::min(first, second);
        constexpr float kCellBoundaryTolerance = 0.0001F;
        return nearest + kCellBoundaryTolerance >= segmentEnter && nearest <= segmentExit + kCellBoundaryTolerance
            ? nearest
            : std::numeric_limits<float>::max();
    };

    const bool vertical = std::abs(direction.x) <= 0.000001F && std::abs(direction.z) <= 0.000001F;
    if (vertical) {
        const float sampleX = (origin->x + terrain.worldSizeX * 0.5F) / cellX;
        const float sampleZ = (origin->z + terrain.worldSizeZ * 0.5F) / cellZ;
        const std::uint32_t cellIndexX = static_cast<std::uint32_t>(std::clamp(
            static_cast<int>(std::floor(sampleX)), 0, static_cast<int>(terrain.width) - 2));
        const std::uint32_t cellIndexZ = static_cast<std::uint32_t>(std::clamp(
            static_cast<int>(std::floor(sampleZ)), 0, static_cast<int>(terrain.height) - 2));
        const float hit = testCell(cellIndexX, cellIndexZ, enter, exit);
        return std::isfinite(hit) && hit < std::numeric_limits<float>::max()
            ? std::optional<kb::scene::Vec3>{ *origin + direction * hit }
            : std::nullopt;
    }

    const float startDistance = std::min(enter + 0.00001F, exit);
    const kb::scene::Vec3 start = *origin + direction * startDistance;
    int cellIndexX = std::clamp(
        static_cast<int>(std::floor((start.x + terrain.worldSizeX * 0.5F) / cellX)),
        0, static_cast<int>(terrain.width) - 2);
    int cellIndexZ = std::clamp(
        static_cast<int>(std::floor((start.z + terrain.worldSizeZ * 0.5F) / cellZ)),
        0, static_cast<int>(terrain.height) - 2);
    const int stepX = direction.x > 0.0F ? 1 : (direction.x < 0.0F ? -1 : 0);
    const int stepZ = direction.z > 0.0F ? 1 : (direction.z < 0.0F ? -1 : 0);
    const float nextBoundaryX = (static_cast<float>(cellIndexX + (stepX > 0 ? 1 : 0)) * cellX) - terrain.worldSizeX * 0.5F;
    const float nextBoundaryZ = (static_cast<float>(cellIndexZ + (stepZ > 0 ? 1 : 0)) * cellZ) - terrain.worldSizeZ * 0.5F;
    float nextX = stepX == 0 ? std::numeric_limits<float>::max() : (nextBoundaryX - origin->x) / direction.x;
    float nextZ = stepZ == 0 ? std::numeric_limits<float>::max() : (nextBoundaryZ - origin->z) / direction.z;
    const float deltaX = stepX == 0 ? std::numeric_limits<float>::max() : cellX / std::abs(direction.x);
    const float deltaZ = stepZ == 0 ? std::numeric_limits<float>::max() : cellZ / std::abs(direction.z);
    float segmentEnter = enter;
    const std::uint32_t maximumVisitedCells = terrain.width + terrain.height;
    for (std::uint32_t visited = 0U; visited < maximumVisitedCells; ++visited) {
        const float segmentExit = std::min({ nextX, nextZ, exit });
        const float hit = testCell(
            static_cast<std::uint32_t>(cellIndexX),
            static_cast<std::uint32_t>(cellIndexZ),
            segmentEnter,
            segmentExit);
        if (hit < std::numeric_limits<float>::max()) return *origin + direction * hit;
        if (segmentExit >= exit) break;
        const bool advanceX = nextX <= nextZ;
        const bool advanceZ = nextZ <= nextX;
        if (advanceX) {
            cellIndexX += stepX;
            nextX += deltaX;
        }
        if (advanceZ) {
            cellIndexZ += stepZ;
            nextZ += deltaZ;
        }
        if (cellIndexX < 0 || cellIndexX >= static_cast<int>(terrain.width) - 1 ||
            cellIndexZ < 0 || cellIndexZ >= static_cast<int>(terrain.height) - 1) break;
        segmentEnter = segmentExit;
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
    const EditorTerrainToolState& tool = EditorTerrainService::ToolState();
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
        .minimumSampleSpacing = tool.mode == EditorTerrainToolMode::Paint &&
                terrain->layerWeightWidth > 1U && terrain->layerWeightHeight > 1U
            ? std::max(
                terrain->worldSizeX / static_cast<float>(terrain->layerWeightWidth - 1U),
                terrain->worldSizeZ / static_cast<float>(terrain->layerWeightHeight - 1U))
            : std::max(
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
    const kb::terrain_editor::TerrainBrushStamp stamp{
        .localX = pointer->local.x,
        .localZ = pointer->local.z };
    kb::terrain_editor::TerrainBrushStamp segmentStart = stamp;
    if (!beginStroke) {
        const float dx = pointer->local.x - tool.lastStampX;
        const float dz = pointer->local.z - tool.lastStampZ;
        const float distance = std::sqrt(dx * dx + dz * dz);
        const float minimumSpacing = std::max(
            pointer->minimumSampleSpacing * 0.5F,
            tool.brush.radius * 0.06F);
        if (distance < minimumSpacing) return true;
        segmentStart.localX = tool.lastStampX;
        segmentStart.localZ = tool.lastStampZ;
    }
    std::string error;
    const bool applied = tool.mode == EditorTerrainToolMode::Paint
        ? sceneContext.ApplyTerrainLayerPaintSegment(
            pointer->entity,
            kb::terrain_editor::TerrainLayerPaintSettings{
                .shape = tool.brush.shape,
                .layerIndex = tool.selectedMaterialLayer,
                .radius = tool.brush.radius,
                .opacity = std::clamp(tool.brush.strength, 0.0F, 1.0F),
                .falloff = tool.brush.falloff,
                .noiseSeed = tool.brush.noiseSeed,
                .erase = (GetKeyState(VK_CONTROL) & 0x8000) != 0,
            },
            segmentStart, stamp,
            beginStroke, &error)
        : sceneContext.ApplyTerrainBrushStamp(
            pointer->entity, tool.brush, stamp, beginStroke, &error);
    if (!applied) {
        sceneContext.Console().Warning("Terrain", error.empty() ? "Terrain brush stamp failed." : error);
        return true;
    }
    tool.strokeActive = true;
    tool.heldSculptElapsedSeconds = 0.0F;
    tool.lastStampX = pointer->local.x;
    tool.lastStampZ = pointer->local.z;
    return true;
}

bool EditorTerrainViewportInteraction::TickActiveStroke(
    EditorSceneContext& sceneContext,
    float deltaSeconds) {
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    if (!tool.strokeActive || !tool.editingEnabled || tool.mode != EditorTerrainToolMode::Sculpt) {
        tool.heldSculptElapsedSeconds = 0.0F;
        return false;
    }
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        tool.strokeActive = false;
        tool.heldSculptElapsedSeconds = 0.0F;
        std::string error;
        if (!sceneContext.CommitTerrainBrushStroke(&error)) {
            sceneContext.Console().Warning(
                "Terrain",
                error.empty() ? "Terrain stroke could not be committed." : error);
        }
        ReleaseCapture();
        return true;
    }
    if (!EditorTerrainStrokeTickPolicy::Advance(deltaSeconds, tool.heldSculptElapsedSeconds)) {
        return false;
    }

    const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (!entity.IsValid() || entity.Id() != tool.hoverEntityId) {
        return false;
    }

    std::string error;
    const bool applied = sceneContext.ApplyTerrainBrushStamp(
        entity,
        tool.brush,
        kb::terrain_editor::TerrainBrushStamp{
            .localX = tool.lastStampX,
            .localZ = tool.lastStampZ,
            .pressure = EditorTerrainStrokeTickPolicy::StampPressure,
        },
        false,
        &error);
    if (!applied) {
        tool.strokeActive = false;
        tool.heldSculptElapsedSeconds = 0.0F;
        ReleaseCapture();
        sceneContext.Console().Warning(
            "Terrain",
            error.empty() ? "Continuous terrain sculpt failed." : error);
    }
    return true;
}

} // namespace kb::editor
#endif
