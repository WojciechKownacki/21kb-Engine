#include "rendering/ScenePanelContentRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include "engine/ecs/SystemSchedulerTrace.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/PhysicsDebugDraw.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/overlay/EditorCameraWireframe.hpp"
#include "scene/EditorSceneSelectionPivot.hpp"
#include "scene/EditorTerrainService.hpp"
#include "scene/EditorViewportCameraState.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace kb::editor {
namespace {

constexpr float kGizmoTargetPixels = 90.0F;
constexpr float kGizmoAxisLength = 1.16F;
constexpr float kMinGizmoDepth = 0.25F;
constexpr float kCameraWireframeWorldDepth = 10.0F;
constexpr std::size_t kEcsOverlayTopSystemCount = 4U;

struct SceneViewportRenderProfileDesc {
    kb::render::SceneRenderMeshPassMode meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueOnly;
    bool shadowPassEnabled = false;
    bool postProcessEnabled = false;
    bool selectionMaskEnabled = false;
    bool selectionOutlineEnabled = false;
    bool gpuDrivenRuntimeDispatchEnabled = false;
    bool autoExposureEnabled = false;
    bool editorStudioLightEnabled = false;
};

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
}

[[nodiscard]] float Aspect(std::uint32_t width, std::uint32_t height) noexcept {
    return height == 0U ? 1.0F : static_cast<float>(std::max(1U, width)) / static_cast<float>(height);
}

// LIB-044: delegates to the single canonical kb::math::ToRadians instead
// of an independently-rederived degrees-to-radians constant.
[[nodiscard]] float DegreesToRadians(float degrees) noexcept {
    return kb::math::ToRadians(kb::math::Degrees{ degrees }).Value();
}

[[nodiscard]] float GizmoScreenSpaceScale(
    const EditorViewportCameraState& camera,
    const EditorViewportCameraAxes& axes,
    kb::scene::Vec3 target,
    std::uint32_t renderHeight) noexcept {
    const float toX = target.x - axes.position.x;
    const float toY = target.y - axes.position.y;
    const float toZ = target.z - axes.position.z;
    const float viewDepth = toX * axes.forward.x + toY * axes.forward.y + toZ * axes.forward.z;
    const float depth = std::max(kMinGizmoDepth, viewDepth);
    const float worldPerPixel = renderHeight == 0U
        ? 1.0F
        : (2.0F * depth * std::tan(DegreesToRadians(camera.VerticalFovDegrees()) * 0.5F)) / static_cast<float>(renderHeight);
    return std::clamp((kGizmoTargetPixels * worldPerPixel) / kGizmoAxisLength, 0.05F, 50000.0F);
}

[[nodiscard]] kb::render::SceneRenderCamera BuildCamera(
    const EditorViewportCameraAxes& axes,
    const kb::scene::CameraComponent& camera,
    std::uint32_t renderWidth,
    std::uint32_t renderHeight) noexcept {
    const kb::scene::Vec3& position = axes.position;
    const bx::Vec3 eye{ position.x, position.y, position.z };
    const bx::Vec3 at{
        position.x + axes.forward.x,
        position.y + axes.forward.y,
        position.z + axes.forward.z,
    };
    const bx::Vec3 up{ axes.up.x, axes.up.y, axes.up.z };

    kb::render::SceneRenderCamera renderCamera{};
    bx::mtxLookAt(renderCamera.view.data(), eye, at, up);
    const bool homogeneousDepth = kb::render::SceneDepthPolicy::HomogeneousDepth();
    if (camera.projection == kb::scene::CameraProjection::Orthographic) {
        kb::render::SceneDepthPolicy::MakeOrthographic(
            renderCamera.projection.data(),
            camera.orthographicHeight,
            Aspect(renderWidth, renderHeight),
            camera.nearClip,
            camera.farClip,
            homogeneousDepth);
    } else {
        kb::render::SceneDepthPolicy::MakePerspective(
            renderCamera.projection.data(),
            camera.verticalFovDegrees,
            Aspect(renderWidth, renderHeight),
            camera.nearClip,
            camera.farClip,
            homogeneousDepth);
    }
    return renderCamera;
}

[[nodiscard]] kb::render::SceneRenderCamera BuildEditorCamera(
    const EditorViewportCameraState& viewportCamera,
    std::uint32_t renderWidth,
    std::uint32_t renderHeight) noexcept {
    kb::scene::CameraComponent camera{};
    camera.verticalFovDegrees = viewportCamera.VerticalFovDegrees();
    camera.nearClip = viewportCamera.NearClip();
    camera.farClip = viewportCamera.FarClip();
    return BuildCamera(
        viewportCamera.Axes(),
        camera,
        renderWidth,
        renderHeight);
}

[[nodiscard]] SceneViewportRenderProfileDesc RenderProfileDesc(EditorViewportRenderProfile profile) noexcept {
    switch (profile) {
    case EditorViewportRenderProfile::Interactive:
        return SceneViewportRenderProfileDesc{
            // Terrain layers above the base layer require blending, but Interactive must not pay for
            // every transparent object or the SceneColor snapshot while editing terrain.
            .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueAndTerrainLayers,
            .shadowPassEnabled = false,
            .postProcessEnabled = true,
            .selectionMaskEnabled = true,
            .selectionOutlineEnabled = true,
            .gpuDrivenRuntimeDispatchEnabled = false,
            .autoExposureEnabled = false,
            .editorStudioLightEnabled = true,
        };
    case EditorViewportRenderProfile::Lit:
        return SceneViewportRenderProfileDesc{
            .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueAndTransparent,
            .shadowPassEnabled = true,
            .postProcessEnabled = true,
            .selectionMaskEnabled = true,
            .selectionOutlineEnabled = true,
            .gpuDrivenRuntimeDispatchEnabled = false,
            .autoExposureEnabled = false,
            .editorStudioLightEnabled = false,
        };
    case EditorViewportRenderProfile::GamePreview:
        return SceneViewportRenderProfileDesc{
            .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueAndTransparent,
            .shadowPassEnabled = true,
            .postProcessEnabled = true,
            .selectionMaskEnabled = true,
            .selectionOutlineEnabled = true,
            .gpuDrivenRuntimeDispatchEnabled = true,
            .autoExposureEnabled = true,
            .editorStudioLightEnabled = false,
        };
    }
    return RenderProfileDesc(EditorViewportRenderProfile::Interactive);
}

[[nodiscard]] kb::render::SceneRenderLightingPath RenderLightingPath(kb::project::ProjectSceneLightingPath path) noexcept {
    switch (path) {
    case kb::project::ProjectSceneLightingPath::Deferred:
        return kb::render::SceneRenderLightingPath::Deferred;
    case kb::project::ProjectSceneLightingPath::ForwardPlus:
        return kb::render::SceneRenderLightingPath::ClusteredForwardPlus;
    case kb::project::ProjectSceneLightingPath::Forward:
    default:
        return kb::render::SceneRenderLightingPath::Forward;
    }
}

[[nodiscard]] kb::render::SceneRenderLightingConfig BuildViewportLightingConfig(
    const SceneViewportRenderProfileDesc& renderProfile,
    kb::project::ProjectSceneLightingPath projectLightingPath) noexcept {
    kb::render::SceneRenderLightingConfig lighting{};
    lighting.lightingPath = RenderLightingPath(projectLightingPath);
    if (lighting.lightingPath == kb::render::SceneRenderLightingPath::ClusteredForwardPlus) {
        lighting.maxForwardLights = kb::render::kMaxSceneForwardPlusLights;
    }
    if (!renderProfile.editorStudioLightEnabled) {
        return lighting;
    }

    lighting.environmentMode = kb::render::SceneRenderEnvironmentMode::Hemisphere;
    lighting.ambientColor = { 0.10F, 0.115F, 0.13F };
    lighting.environmentZenithColor = { 0.34F, 0.40F, 0.48F };
    lighting.environmentGroundColor = { 0.055F, 0.06F, 0.07F };
    lighting.environmentDiffuseIntensity = 0.70F;
    lighting.environmentSpecularIntensity = 0.18F;
    lighting.editorPreviewKeyLightEnabled = true;
    lighting.editorPreviewKeyLightDirection = { 0.35F, -0.62F, 0.70F };
    lighting.editorPreviewKeyLightColor = { 1.0F, 0.96F, 0.90F };
    lighting.editorPreviewKeyLightIntensity = 1.85F;
    return lighting;
}

[[nodiscard]] std::vector<std::uint64_t> SelectedEntityIds(const EditorSceneContext& sceneContext) {
    std::vector<std::uint64_t> ids;
    const std::vector<kb::scene::SceneEntity>& selected = sceneContext.SelectedHierarchyEntities();
    ids.reserve(selected.size());
    for (const kb::scene::SceneEntity entity : selected) {
        if (entity.IsValid()) {
            ids.push_back(entity.Id());
        }
    }
    return ids;
}

[[nodiscard]] bool IsSelectedEntity(const EditorSceneContext& sceneContext, kb::scene::SceneEntity entity) {
    if (!entity.IsValid()) {
        return false;
    }

    const std::vector<kb::scene::SceneEntity>& selected = sceneContext.SelectedHierarchyEntities();
    return std::find(selected.begin(), selected.end(), entity) != selected.end();
}

struct LightWireframeBasis {
    std::array<float, 3> right{1.0F, 0.0F, 0.0F};
    std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    std::array<float, 3> forward{0.0F, 0.0F, 1.0F};
};

[[nodiscard]] LightWireframeBasis BasisFromQuat(kb::scene::Quat q) noexcept {
    const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (lengthSquared > 0.000001F) {
        const float invLength = 1.0F / std::sqrt(lengthSquared);
        q.x *= invLength;
        q.y *= invLength;
        q.z *= invLength;
        q.w *= invLength;
    } else {
        q = kb::scene::Quat{};
    }

    const float x2 = q.x + q.x;
    const float y2 = q.y + q.y;
    const float z2 = q.z + q.z;
    const float xx = q.x * x2;
    const float xy = q.x * y2;
    const float xz = q.x * z2;
    const float yy = q.y * y2;
    const float yz = q.y * z2;
    const float zz = q.z * z2;
    const float wx = q.w * x2;
    const float wy = q.w * y2;
    const float wz = q.w * z2;

    return LightWireframeBasis{
        .right = {1.0F - (yy + zz), xy + wz, xz - wy},
        .up = {xy - wz, 1.0F - (xx + zz), yz + wx},
        .forward = {xz + wy, yz - wx, 1.0F - (xx + yy)},
    };
}

[[nodiscard]] std::array<float, 3> LightWireframeColor(const kb::scene::LightComponent& light) noexcept {
    std::array<float, 3> color{
        std::clamp(light.color.x, 0.0F, 1.0F),
        std::clamp(light.color.y, 0.0F, 1.0F),
        std::clamp(light.color.z, 0.0F, 1.0F),
    };
    const float brightest = std::max({color[0], color[1], color[2]});
    if (brightest < 0.18F) {
        color = {1.0F, 0.86F, 0.32F};
    }
    return color;
}

[[nodiscard]] kb::render::EditorLightWireframeKind ToEditorLightWireframeKind(kb::scene::LightKind kind) noexcept {
    switch (kind) {
    case kb::scene::LightKind::Spot:
        return kb::render::EditorLightWireframeKind::Spot;
    case kb::scene::LightKind::Directional:
        return kb::render::EditorLightWireframeKind::Directional;
    case kb::scene::LightKind::Point:
    default:
        return kb::render::EditorLightWireframeKind::Point;
    }
}

[[nodiscard]] std::array<float, 3> ToArray(kb::scene::Vec3 value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] kb::scene::Vec3 ResolveWorldPosition(const kb::scene::TransformComponent& transform) noexcept {
    return transform.worldDirty ? transform.localPosition : transform.worldPosition;
}

[[nodiscard]] kb::scene::Quat ResolveWorldRotation(const kb::scene::TransformComponent& transform) noexcept {
    return transform.worldDirty ? transform.localRotation : transform.worldRotation;
}

[[nodiscard]] float CameraWireframeDisplayFarClip(
    const kb::scene::CameraComponent& camera) noexcept {
    const float nearClip = std::max(0.0001F, camera.nearClip);
    const float authoredFarClip =
        std::max(nearClip + 0.0001F, camera.farClip);
    const float minimumVisibleDepth = nearClip + 0.001F;
    return std::min(
        authoredFarClip,
        std::max(minimumVisibleDepth, kCameraWireframeWorldDepth));
}

[[nodiscard]] std::vector<kb::render::EditorCameraWireframeDesc> BuildCameraWireframes(
    const EditorSceneContext& sceneContext) {
    struct Context {
        const EditorSceneContext* sceneContext = nullptr;
        std::vector<kb::render::EditorCameraWireframeDesc> wireframes;
    } context{
        .sceneContext = &sceneContext,
    };

    sceneContext.Scene().Components().Visitors().ForEachCamera(
        [](kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::CameraComponent& camera, void* opaque) {
            auto& context = *static_cast<Context*>(opaque);
            if (!IsSelectedEntity(*context.sceneContext, entity) ||
                !context.sceneContext->Scene().Components().Visibility().Get(entity).visible) {
                return;
            }

            const LightWireframeBasis basis = BasisFromQuat(ResolveWorldRotation(transform));
            const kb::scene::Vec3 position = ResolveWorldPosition(transform);
            context.wireframes.push_back(kb::render::EditorCameraWireframeDesc{
                .projection = camera.projection == kb::scene::CameraProjection::Orthographic
                    ? kb::render::EditorCameraWireframeProjection::Orthographic
                    : kb::render::EditorCameraWireframeProjection::Perspective,
                .position = {position.x, position.y, position.z},
                .forward = basis.forward,
                .right = basis.right,
                .up = basis.up,
                .verticalFovDegrees = camera.verticalFovDegrees,
                .orthographicHeight = camera.orthographicHeight,
                .nearClip = camera.nearClip,
                .farClip = camera.farClip,
                // Keep the authoring frustum closed without changing
                // Camera.farClip. A fixed world-space depth makes the gizmo
                // naturally shrink/grow as Scene View moves away/towards it.
                .displayFarClip = CameraWireframeDisplayFarClip(camera),
                // CameraComponent does not own the editor fly-camera viewport.
                // Keep its authoring wireframe square instead of inheriting the
                // unrelated Scene View panel aspect ratio.
                .aspect = 1.0F,
            });
        },
        &context);

    return context.wireframes;
}

[[nodiscard]] std::vector<kb::render::EditorLightWireframeDesc> BuildLightWireframes(
    const EditorSceneContext& sceneContext,
    const EditorViewportCameraState& viewportCamera,
    const EditorViewportCameraAxes& viewportAxes,
    std::uint32_t renderHeight) {
    struct Context {
        const EditorSceneContext* sceneContext = nullptr;
        const EditorViewportCameraState* viewportCamera = nullptr;
        const EditorViewportCameraAxes* viewportAxes = nullptr;
        std::uint32_t renderHeight = 0U;
        std::vector<kb::render::EditorLightWireframeDesc> wireframes;
    } context{
        .sceneContext = &sceneContext,
        .viewportCamera = &viewportCamera,
        .viewportAxes = &viewportAxes,
        .renderHeight = renderHeight,
    };

    sceneContext.Scene().Components().Visitors().ForEachLight(
        [](kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::LightComponent& light, void* opaque) {
            auto& context = *static_cast<Context*>(opaque);
            if (!context.sceneContext->Scene().Components().Visibility().Get(entity).visible) {
                return;
            }

            const LightWireframeBasis basis = BasisFromQuat(ResolveWorldRotation(transform));
            const kb::scene::Vec3 position = ResolveWorldPosition(transform);
            context.wireframes.push_back(kb::render::EditorLightWireframeDesc{
                .kind = ToEditorLightWireframeKind(light.kind),
                .position = {position.x, position.y, position.z},
                .forward = basis.forward,
                .right = basis.right,
                .up = basis.up,
                .iconRight = ToArray(context.viewportAxes->right),
                .iconUp = ToArray(context.viewportAxes->up),
                .color = LightWireframeColor(light),
                .range = light.kind == kb::scene::LightKind::Directional ? 0.0F : std::max(0.0F, light.range),
                .outerConeDegrees = light.outerConeDegrees,
                .iconWorldScale = GizmoScreenSpaceScale(*context.viewportCamera, *context.viewportAxes, position, context.renderHeight) * 0.30F,
                .selected = IsSelectedEntity(*context.sceneContext, entity),
            });
        },
        &context);

    return context.wireframes;
}

// LIB-132: honest empty vector (zero cost beyond the IsEnabled check) whenever debug draw is
// off - which it is by default, and which kb_standalone_player never turns on (it never calls
// this function at all, see PhysicsDebugDraw.hpp's own "zero release-path impact" comment).
[[nodiscard]] float TerrainHeightAt(
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
    const auto height = [&terrain](std::uint32_t x, std::uint32_t z) {
        return terrain.heights[static_cast<std::size_t>(z) * terrain.width + x];
    };
    const float top = std::lerp(height(x0, z0), height(x1, z0), sampleX - static_cast<float>(x0));
    const float bottom = std::lerp(height(x0, z1), height(x1, z1), sampleX - static_cast<float>(x0));
    return std::lerp(top, bottom, sampleZ - static_cast<float>(z0));
}

[[nodiscard]] kb::scene::Vec3 TerrainPointToWorld(
    const kb::scene::TransformComponent& transform,
    kb::scene::Vec3 local) noexcept {
    local.x *= transform.worldScale.x;
    local.y *= transform.worldScale.y;
    local.z *= transform.worldScale.z;
    return transform.worldPosition + kb::math::Rotate(transform.worldRotation, local);
}

void AppendTerrainBrushRing(
    const EditorSceneContext& sceneContext,
    std::vector<kb::render::PhysicsDebugLine>& lines) {
    const EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    const kb::scene::SceneEntity entity{ tool.hoverEntityId };
    if (!tool.editingEnabled || tool.mode == EditorTerrainToolMode::Select ||
        !tool.hoverVisible || entity != sceneContext.SelectedEntity() ||
        !sceneContext.IsProjectPluginEnabled("Editor.Terrain")) {
        return;
    }
    const kb::assets::TerrainAsset* terrain = sceneContext.TerrainForEditing(entity);
    if (terrain == nullptr) return;
    const kb::scene::TransformComponent transform = sceneContext.Scene().Transforms().Get(entity);
    constexpr std::size_t segmentCount = 64U;
    constexpr float twoPi = 6.28318530717958647692F;
    const std::array<float, 3U> color = tool.mode == EditorTerrainToolMode::Holes
        ? std::array<float, 3U>{ 1.0F, 0.38F, 0.12F }
        : tool.mode == EditorTerrainToolMode::Paint
            ? std::array<float, 3U>{ 1.0F, 0.63F, 0.16F }
            : std::array<float, 3U>{ 0.16F, 0.88F, 1.0F };
    const auto point = [&](std::size_t index) -> std::optional<kb::scene::Vec3> {
        const float angle = twoPi * static_cast<float>(index) / static_cast<float>(segmentCount);
        const float x = tool.hoverLocalX + std::cos(angle) * tool.brush.radius;
        const float z = tool.hoverLocalZ + std::sin(angle) * tool.brush.radius;
        if (std::abs(x) > terrain->worldSizeX * 0.5F ||
            std::abs(z) > terrain->worldSizeZ * 0.5F) {
            return std::nullopt;
        }
        return TerrainPointToWorld(
            transform,
            kb::scene::Vec3{ x, TerrainHeightAt(*terrain, x, z) + 0.04F, z });
    };
    std::optional<kb::scene::Vec3> from = point(0U);
    for (std::size_t index = 1U; index <= segmentCount; ++index) {
        const std::optional<kb::scene::Vec3> to = point(index % segmentCount);
        if (from.has_value() && to.has_value()) {
            lines.push_back(kb::render::PhysicsDebugLine{
                .from = { from->x, from->y, from->z },
                .to = { to->x, to->y, to->z },
                .color = color,
                .alpha = 1.0F,
            });
        }
        from = to;
    }
}

[[nodiscard]] std::vector<kb::render::PhysicsDebugLine> BuildPhysicsDebugLines(const EditorSceneContext& sceneContext) {
    std::vector<kb::render::PhysicsDebugLine> lines;
    if (kb::scene::PhysicsDebugDraw::IsEnabled(sceneContext.Scene())) {
        const std::vector<kb::scene::PhysicsDebugLineDesc> shapes =
            kb::scene::PhysicsDebugDraw::CollectLines(sceneContext.Scene());
        lines.reserve(shapes.size() + 64U);
        for (const kb::scene::PhysicsDebugLineDesc& shape : shapes) {
            lines.push_back(kb::render::PhysicsDebugLine{
                .from = { shape.from.x, shape.from.y, shape.from.z },
                .to = { shape.to.x, shape.to.y, shape.to.z },
                .color = { shape.color.x, shape.color.y, shape.color.z },
                .alpha = shape.alpha,
            });
        }
    }
    AppendTerrainBrushRing(sceneContext, lines);
    return lines;
}

[[nodiscard]] SceneViewportToolbarEcsStats BuildEcsStats(const EditorSceneContext& sceneContext) {
    const kb::ecs::SystemSchedulerTrace& trace = sceneContext.Scene().Runtime().LastEcsProfilerTrace();
    SceneViewportToolbarEcsStats stats{
        .frameIndex = trace.frameCounters.frameIndex,
        .frameDurationNanoseconds = trace.frameCounters.frameDurationNanoseconds,
        .cpuTimeNanoseconds = trace.frameCounters.cpuTimeNanoseconds,
        .jobsCount = trace.frameCounters.jobsCount,
        .entitiesProcessed = trace.frameCounters.entitiesProcessed,
        .bytesTouched = trace.frameCounters.bytesTouched,
        .systemCount = static_cast<std::uint64_t>(trace.frameCounters.systemCount),
        .workerCount = static_cast<std::uint64_t>(trace.frameCounters.workerCount),
        .valid = sceneContext.Scene().Runtime().EcsProfilerEnabled() && trace.frameCounters.frameDurationNanoseconds > 0U,
    };

    stats.topSystems.reserve(std::min(kEcsOverlayTopSystemCount, trace.systemCounters.size()));
    std::vector<const kb::ecs::SystemSchedulerSystemCounters*> sortedSystems;
    sortedSystems.reserve(trace.systemCounters.size());
    for (const kb::ecs::SystemSchedulerSystemCounters& system : trace.systemCounters) {
        sortedSystems.push_back(&system);
    }
    std::sort(sortedSystems.begin(), sortedSystems.end(), [](const kb::ecs::SystemSchedulerSystemCounters* lhs, const kb::ecs::SystemSchedulerSystemCounters* rhs) {
        if (lhs->cpuTimeNanoseconds != rhs->cpuTimeNanoseconds) {
            return lhs->cpuTimeNanoseconds > rhs->cpuTimeNanoseconds;
        }
        return lhs->systemName < rhs->systemName;
    });

    const std::size_t count = std::min(kEcsOverlayTopSystemCount, sortedSystems.size());
    for (std::size_t index = 0; index < count; ++index) {
        const kb::ecs::SystemSchedulerSystemCounters& system = *sortedSystems[index];
        stats.topSystems.push_back(SceneViewportToolbarEcsSystemStat{
            .name = system.systemName,
            .cpuTimeNanoseconds = system.cpuTimeNanoseconds,
            .jobsCount = system.jobsCount,
            .entitiesProcessed = system.entitiesProcessed,
            .bytesTouched = system.bytesTouched,
        });
    }
    return stats;
}

[[nodiscard]] RECT SelectionBoxLocalRect(const EditorSceneViewportBoxSelectionState& selection) noexcept {
    RECT rect{selection.start.x, selection.start.y, selection.current.x, selection.current.y};
    if (rect.left > rect.right) {
        std::swap(rect.left, rect.right);
    }
    if (rect.top > rect.bottom) {
        std::swap(rect.top, rect.bottom);
    }
    return rect;
}

[[nodiscard]] kb::render::RenderSceneSubmitDesc::EditorSelectionBoxDesc SelectionBoxDesc(
    const EditorSceneContext& sceneContext,
    std::uint32_t panelId) noexcept {
    const EditorSceneViewportBoxSelectionState& selection = sceneContext.ViewportBoxSelection();
    if (!selection.active || selection.panelId != panelId) {
        return {};
    }

    const RECT rect = SelectionBoxLocalRect(selection);
    return kb::render::RenderSceneSubmitDesc::EditorSelectionBoxDesc{
        .x = static_cast<float>(rect.left),
        .y = static_cast<float>(rect.top),
        .width = static_cast<float>(std::max<LONG>(0, rect.right - rect.left)),
        .height = static_cast<float>(std::max<LONG>(0, rect.bottom - rect.top)),
        .visible = true,
    };
}

[[nodiscard]] EditorSceneBgfxViewport::PresentSettings BuildViewportPresentSettings(
    const EditorSceneContext& sceneContext,
    std::uint32_t panelId,
    DockPanelKind panelKind,
    const EditorViewportPreviewState& viewportState,
    const EditorRenderBackendSettings& renderBackendSettings,
    const SceneViewportToolbarRects& sceneRects) {
    static_cast<void>(panelKind);
    const EditorViewportProfile profile = viewportState.Profile();
    const SceneViewportRenderProfileDesc renderProfile = RenderProfileDesc(viewportState.RenderProfile());
    const bool msaaMode = renderBackendSettings.MsaaSamples() > 0U;
    // MSAA only replaces the AA method, not the rest of the pipeline: the scene target's resolved
    // (single-sample) color already feeds the post-process chain the same way FXAA/TAA/no-AA do
    // (see Renderer::SubmitSceneToViewport's ResolveSceneColorForSampling + postProcessChain_.Evaluate),
    // so bloom/tonemap/selection outline must stay enabled under MSAA instead of being silently dropped.
    const bool postProcessEnabled = renderProfile.postProcessEnabled && renderBackendSettings.PostProcessEnabled();
    // Edit mode is event-driven: a viewport may sit idle between isolated mouse events. Advancing
    // temporal history or exposure only on those events makes a static grid jump in brightness and
    // aliasing, which reads as camera motion. Runtime play renders continuously, so temporal sampling
    // remains enabled there; edit mode uses deterministic FXAA when the project requests TAA.
    const bool continuousRuntimeFrames = sceneContext.HasPlayModeSceneSession();
    const bool requestedTemporalAntiAliasing = renderBackendSettings.TemporalAntiAliasingEnabled();
    kb::render::ScenePostProcessSettings postProcessSettings{};
    postProcessSettings.fxaaEnabled = renderBackendSettings.FxaaEnabled() ||
        (requestedTemporalAntiAliasing && !continuousRuntimeFrames);
    postProcessSettings.temporalAntiAliasingEnabled =
        requestedTemporalAntiAliasing && continuousRuntimeFrames;
    postProcessSettings.temporalJitterEnabled =
        requestedTemporalAntiAliasing && continuousRuntimeFrames;
    postProcessSettings.bloomEnabled = renderBackendSettings.BloomEnabled();
    postProcessSettings.outputTransform.autoExposure.enabled =
        renderProfile.autoExposureEnabled && continuousRuntimeFrames;
    postProcessSettings.outputTransform.autoExposure.temporalAdaptationEnabled =
        renderProfile.autoExposureEnabled && continuousRuntimeFrames;
    kb::render::SceneRenderLightingConfig lightingConfig = BuildViewportLightingConfig(renderProfile, sceneContext.Project().sceneLightingPath);
    // SceneGBuffer has no MSAA-attachment support, so a multisampled Deferred G-buffer isn't an
    // option today; falling back to Forward is the only way to honor the MSAA request. This is a
    // real cost (per-sample forward shading instead of a single per-pixel deferred resolve), not a
    // bug — see the AntiAliasingMode::Msaa tooltip in ProjectSettingsPanelRenderer for the user-facing
    // explanation.
    const bool msaaForcesForward = msaaMode && lightingConfig.lightingPath == kb::render::SceneRenderLightingPath::Deferred;
    if (msaaForcesForward) {
        lightingConfig.lightingPath = kb::render::SceneRenderLightingPath::Forward;
        lightingConfig.maxForwardLights = kb::render::kMaxSceneForwardLights;
    }
    const std::uint32_t renderWidth = viewportState.RenderWidthForPanel(RectWidth(sceneRects.renderArea));
    const std::uint32_t renderHeight = viewportState.RenderHeightForPanel(RectHeight(sceneRects.renderArea));
    const EditorViewportCameraState& viewportCamera = sceneContext.ViewportCamera(panelId);
    const EditorViewportCameraAxes axes = viewportCamera.Axes();
    kb::render::RenderSceneSubmitDesc::EditorGizmoDesc gizmo{};
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    const EditorTerrainToolState& terrainTool = EditorTerrainService::ToolState();
    const bool terrainEditing = terrainTool.editingEnabled &&
        terrainTool.mode != EditorTerrainToolMode::Select &&
        EditorTerrainService::IsTerrainEntity(sceneContext.Scene(), selected);
    if (!terrainEditing && selected.IsValid() && sceneContext.Scene().Entities().IsAlive(selected)) {
        if (const std::optional<kb::scene::Vec3> pivot = EditorSceneSelectionPivot::Resolve(sceneContext.Scene(), sceneContext.SelectedHierarchyEntities(), selected)) {
            const kb::scene::Vec3 target = *pivot;
            gizmo.visible = true;
            gizmo.targetPosition = {target.x, target.y, target.z};
            gizmo.worldScale = GizmoScreenSpaceScale(viewportCamera, axes, target, renderHeight);
            gizmo.hoveredAxis = sceneContext.Gizmo().hoveredAxis;
            gizmo.draggedAxis = sceneContext.Gizmo().draggedAxis;
            gizmo.mode = static_cast<std::uint8_t>(sceneContext.Gizmo().toolMode);
        }
    }

    return EditorSceneBgfxViewport::PresentSettings{
        .renderWidth = renderWidth,
        .renderHeight = renderHeight,
        .fitMode = viewportState.FitMode(),
        .safeArea = profile.safeArea,
        .cameraOverride = BuildEditorCamera(viewportCamera, renderWidth, renderHeight),
        .selectedEntityIds = SelectedEntityIds(sceneContext),
        .viewportKey = panelId,
        .editorSceneOverlaysEnabled = true,
        .editorGrid = kb::render::RenderSceneSubmitDesc::EditorGridDesc{
            .minorSpacingMeters = viewportState.GridSpacing(),
            .majorEvery = viewportState.GridMajorEvery(),
            .visible = viewportState.GridVisible(),
        },
        .editorGizmo = gizmo,
        .editorCameraWireframes = BuildCameraWireframes(sceneContext),
        .editorLightWireframes = BuildLightWireframes(sceneContext, viewportCamera, axes, renderHeight),
        .physicsDebugLines = BuildPhysicsDebugLines(sceneContext),
        .editorSelectionBox = SelectionBoxDesc(sceneContext, panelId),
        .meshPassMode = renderProfile.meshPassMode,
        .lightingConfig = lightingConfig,
        .postProcessSettings = postProcessSettings,
        .msaaSamples = renderBackendSettings.MsaaSamples(),
        .shadowPassEnabled = renderProfile.shadowPassEnabled && renderBackendSettings.ShadowsEnabled(),
        .postProcessEnabled = postProcessEnabled,
        .selectionMaskEnabled = postProcessEnabled && renderProfile.selectionMaskEnabled,
        .selectionOutlineEnabled = postProcessEnabled && renderProfile.selectionOutlineEnabled && renderBackendSettings.SelectionOutlineEnabled(),
        .gpuDrivenRuntimeDispatchEnabled = renderProfile.gpuDrivenRuntimeDispatchEnabled && renderBackendSettings.GpuDrivenEnabled(),
        .drawSafeArea = profile.devicePreview,
        .sceneRevision = sceneContext.SceneRenderRevision(),
        .sceneDirtyBaseRevision = sceneContext.SceneRenderDirtyBaseRevision(),
        .sceneFullSyncRequired = sceneContext.SceneRenderFullDirty(),
        .dirtySceneEntityIds = sceneContext.SceneRenderDirtyEntityIds(),
    };
}

} // namespace

void ScenePanelContentRenderer::PresentViewport(
    EditorSceneBgfxViewport& sceneViewport,
    HWND sceneViewportHost,
    const RECT& content,
    const DockPanel& panel,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings) {
    if (sceneViewportHost == nullptr) {
        return;
    }

    const EditorViewportPreviewState& viewportState = sceneContext.ViewportPreview(panel.id);
    const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(content, viewportState, sceneContext);
    const EditorSceneBgfxViewport::PresentSettings settings = BuildViewportPresentSettings(sceneContext, panel.id, panel.kind, viewportState, renderBackendSettings, sceneRects);

    sceneViewport.Present(sceneViewportHost, sceneRects.renderArea, sceneContext.Scene(), settings);
}

void ScenePanelContentRenderer::Paint(
    HDC dc,
    const RECT& content,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport* sceneViewport,
    HWND sceneViewportHost) const {
    const EditorViewportPreviewState& viewportState = sceneContext.ViewportPreview(panel.id);
    SceneViewportToolbarRenderer::RecordEcsStats(BuildEcsStats(sceneContext));
    SceneViewportToolbarRenderer::Paint(dc, content, theme, viewportState);
    SceneViewportToolbarRenderer::PaintTerrainTools(dc, content, theme, sceneContext);

    if (sceneViewport == nullptr) {
        return;
    }

    const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(content, viewportState, sceneContext);
    const EditorSceneBgfxViewport::PresentSettings settings = BuildViewportPresentSettings(sceneContext, panel.id, panel.kind, viewportState, renderBackendSettings, sceneRects);
    if (sceneViewportHost != nullptr) {
        sceneViewport->Present(dc, sceneViewportHost, sceneRects.renderArea, sceneContext.Scene(), theme, settings);
    } else {
        sceneViewport->Present(dc, sceneRects.renderArea, sceneContext.Scene(), theme, settings);
    }
}

} // namespace kb::editor

#endif
