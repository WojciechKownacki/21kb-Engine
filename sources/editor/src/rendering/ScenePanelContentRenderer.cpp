#include "rendering/ScenePanelContentRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "kb/render/SceneDepthPolicy.hpp"

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

struct CameraBasis {
    float xx = 1.0F;
    float xy = 0.0F;
    float xz = 0.0F;
    float yx = 0.0F;
    float yy = 1.0F;
    float yz = 0.0F;
    float zx = 0.0F;
    float zy = 0.0F;
    float zz = 1.0F;
};

[[nodiscard]] CameraBasis BasisFromQuat(const kb::scene::Quat& q) noexcept {
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

    return CameraBasis{
        .xx = 1.0F - (yy + zz),
        .xy = xy + wz,
        .xz = xz - wy,
        .yx = xy - wz,
        .yy = 1.0F - (xx + zz),
        .yz = yz + wx,
        .zx = xz + wy,
        .zy = yz - wx,
        .zz = 1.0F - (xx + yy),
    };
}

[[nodiscard]] kb::render::SceneRenderCamera BuildCamera(
    const kb::scene::Vec3& position,
    const kb::scene::Quat& rotation,
    const kb::scene::CameraComponent& camera,
    std::uint32_t renderWidth,
    std::uint32_t renderHeight) noexcept {
    const CameraBasis basis = BasisFromQuat(rotation);
    const bx::Vec3 eye{ position.x, position.y, position.z };
    const bx::Vec3 at{
        position.x + basis.zx,
        position.y + basis.zy,
        position.z + basis.zz,
    };
    const bx::Vec3 up{ basis.yx, basis.yy, basis.yz };

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

[[nodiscard]] kb::render::SceneRenderCamera BuildEditorCamera(std::uint32_t renderWidth, std::uint32_t renderHeight) noexcept {
    kb::scene::CameraComponent camera{};
    camera.verticalFovDegrees = 60.0F;
    camera.nearClip = 0.01F;
    camera.farClip = 1000.0F;
    return BuildCamera(
        kb::scene::Vec3{ 0.0F, 2.0F, -6.0F },
        kb::scene::Quat{},
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
        .cameraOverride = BuildEditorCamera(renderWidth, renderHeight),
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
