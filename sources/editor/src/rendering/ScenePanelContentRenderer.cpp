#include "rendering/ScenePanelContentRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include "engine/scene/CameraComponent.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "scene/EditorViewportCameraState.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
}

[[nodiscard]] float Aspect(std::uint32_t width, std::uint32_t height) noexcept {
    return height == 0U ? 1.0F : static_cast<float>(std::max(1U, width)) / static_cast<float>(height);
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

[[nodiscard]] EditorSceneBgfxViewport::PresentSettings BuildViewportPresentSettings(
    const EditorSceneContext& sceneContext,
    std::uint32_t panelId,
    DockPanelKind panelKind,
    const EditorViewportPreviewState& viewportState,
    const SceneViewportToolbarRects& sceneRects) {
    static_cast<void>(panelKind);
    const EditorViewportProfile profile = viewportState.Profile();
    const std::uint32_t renderWidth = viewportState.RenderWidthForPanel(RectWidth(sceneRects.renderArea));
    const std::uint32_t renderHeight = viewportState.RenderHeightForPanel(RectHeight(sceneRects.renderArea));

    return EditorSceneBgfxViewport::PresentSettings{
        .renderWidth = renderWidth,
        .renderHeight = renderHeight,
        .fitMode = viewportState.FitMode(),
        .safeArea = profile.safeArea,
        .cameraOverride = BuildEditorCamera(sceneContext.ViewportCamera(panelId), renderWidth, renderHeight),
        .selectedEntityIds = std::array<std::uint64_t, 1U>{ sceneContext.SelectedEntity().Id() },
        .viewportKey = panelId,
        .editorSceneOverlaysEnabled = true,
        .drawSafeArea = profile.devicePreview,
    };
}

} // namespace

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

    const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(content);
    const EditorSceneBgfxViewport::PresentSettings settings = BuildViewportPresentSettings(sceneContext, panel.id, panel.kind, viewportState, sceneRects);
    if (sceneViewportHost != nullptr) {
        sceneViewport->Present(dc, sceneViewportHost, sceneRects.renderArea, sceneContext.Scene(), theme, settings);
    } else {
        sceneViewport->Present(dc, sceneRects.renderArea, sceneContext.Scene(), theme, settings);
    }
}

} // namespace kb::editor

#endif
