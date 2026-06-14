#include "rendering/ScenePanelContentRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "scene/EditorSceneSelectionPivot.hpp"
#include "scene/EditorViewportCameraState.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

namespace kb::editor {
namespace {

constexpr float kGizmoTargetPixels = 90.0F;
constexpr float kGizmoAxisLength = 1.16F;
constexpr float kMinGizmoDepth = 0.25F;

struct SceneViewportRenderProfileDesc {
    kb::render::SceneRenderMeshPassMode meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueOnly;
    bool shadowPassEnabled = false;
    bool postProcessEnabled = false;
    bool selectionMaskEnabled = false;
    bool selectionOutlineEnabled = false;
    bool gpuDrivenRuntimeDispatchEnabled = false;
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

[[nodiscard]] float DegreesToRadians(float degrees) noexcept {
    return degrees * 3.14159265358979323846F / 180.0F;
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
            .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueOnly,
            .shadowPassEnabled = false,
            .postProcessEnabled = true,
            .selectionMaskEnabled = true,
            .selectionOutlineEnabled = true,
            .gpuDrivenRuntimeDispatchEnabled = false,
        };
    case EditorViewportRenderProfile::Lit:
        return SceneViewportRenderProfileDesc{
            .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueAndTransparent,
            .shadowPassEnabled = true,
            .postProcessEnabled = true,
            .selectionMaskEnabled = true,
            .selectionOutlineEnabled = true,
            .gpuDrivenRuntimeDispatchEnabled = false,
        };
    case EditorViewportRenderProfile::GamePreview:
        return SceneViewportRenderProfileDesc{
            .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueAndTransparent,
            .shadowPassEnabled = true,
            .postProcessEnabled = true,
            .selectionMaskEnabled = true,
            .selectionOutlineEnabled = true,
            .gpuDrivenRuntimeDispatchEnabled = true,
        };
    }
    return RenderProfileDesc(EditorViewportRenderProfile::Interactive);
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
    const SceneViewportToolbarRects& sceneRects) {
    static_cast<void>(panelKind);
    const EditorViewportProfile profile = viewportState.Profile();
    const SceneViewportRenderProfileDesc renderProfile = RenderProfileDesc(viewportState.RenderProfile());
    const std::uint32_t renderWidth = viewportState.RenderWidthForPanel(RectWidth(sceneRects.renderArea));
    const std::uint32_t renderHeight = viewportState.RenderHeightForPanel(RectHeight(sceneRects.renderArea));
    const EditorViewportCameraState& viewportCamera = sceneContext.ViewportCamera(panelId);
    const EditorViewportCameraAxes axes = viewportCamera.Axes();
    kb::render::RenderSceneSubmitDesc::EditorGizmoDesc gizmo{};
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (selected.IsValid() && sceneContext.Scene().Entities().IsAlive(selected)) {
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
        .editorSelectionBox = SelectionBoxDesc(sceneContext, panelId),
        .meshPassMode = renderProfile.meshPassMode,
        .shadowPassEnabled = renderProfile.shadowPassEnabled,
        .postProcessEnabled = renderProfile.postProcessEnabled,
        .selectionMaskEnabled = renderProfile.selectionMaskEnabled,
        .selectionOutlineEnabled = renderProfile.selectionOutlineEnabled,
        .gpuDrivenRuntimeDispatchEnabled = renderProfile.gpuDrivenRuntimeDispatchEnabled,
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
    const EditorSceneContext& sceneContext) {
    if (sceneViewportHost == nullptr) {
        return;
    }

    const EditorViewportPreviewState& viewportState = sceneContext.ViewportPreview(panel.id);
    const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(content, viewportState);
    const EditorSceneBgfxViewport::PresentSettings settings = BuildViewportPresentSettings(sceneContext, panel.id, panel.kind, viewportState, sceneRects);

    sceneViewport.BeginPaintLayout(sceneViewportHost);
    sceneViewport.Present(sceneViewportHost, sceneRects.renderArea, sceneContext.Scene(), settings);
    sceneViewport.EndPaintLayout();
    sceneViewport.ClearPresentRequest();
}

void ScenePanelContentRenderer::Paint(
    HDC dc,
    const RECT& content,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport* sceneViewport,
    HWND sceneViewportHost) const {
    const EditorViewportPreviewState& viewportState = sceneContext.ViewportPreview(panel.id);
    SceneViewportToolbarRenderer::Paint(dc, content, theme, viewportState);

    if (sceneViewport == nullptr) {
        return;
    }

    const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(content, viewportState);
    const EditorSceneBgfxViewport::PresentSettings settings = BuildViewportPresentSettings(sceneContext, panel.id, panel.kind, viewportState, sceneRects);
    if (sceneViewportHost != nullptr) {
        sceneViewport->Present(dc, sceneViewportHost, sceneRects.renderArea, sceneContext.Scene(), theme, settings);
    } else {
        sceneViewport->Present(dc, sceneRects.renderArea, sceneContext.Scene(), theme, settings);
    }
}

} // namespace kb::editor

#endif
