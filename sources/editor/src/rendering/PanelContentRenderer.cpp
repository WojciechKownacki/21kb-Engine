#include "rendering/PanelContentRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HierarchyPanelRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/ProjectFilesPanelRenderer.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "kb/render/SceneDepthPolicy.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
}

[[nodiscard]] RECT CenteredRectFor(const RECT& bounds, std::uint32_t renderWidth, std::uint32_t renderHeight, EditorViewportFitMode fitMode) noexcept {
    const std::uint32_t boundsWidth = RectWidth(bounds);
    const std::uint32_t boundsHeight = RectHeight(bounds);
    if (boundsWidth == 0U || boundsHeight == 0U || renderWidth == 0U || renderHeight == 0U) {
        return bounds;
    }

    const double scaleX = static_cast<double>(boundsWidth) / static_cast<double>(renderWidth);
    const double scaleY = static_cast<double>(boundsHeight) / static_cast<double>(renderHeight);
    double scale = std::min(scaleX, scaleY);
    if (fitMode == EditorViewportFitMode::OneToOne) {
        scale = 1.0;
    } else if (fitMode == EditorViewportFitMode::Fill) {
        scale = std::max(scaleX, scaleY);
    }

    const LONG width = std::max<LONG>(1, static_cast<LONG>(std::lround(static_cast<double>(renderWidth) * scale)));
    const LONG height = std::max<LONG>(1, static_cast<LONG>(std::lround(static_cast<double>(renderHeight) * scale)));
    const LONG centerX = bounds.left + static_cast<LONG>(boundsWidth / 2U);
    const LONG centerY = bounds.top + static_cast<LONG>(boundsHeight / 2U);
    return RECT{
        .left = centerX - width / 2,
        .top = centerY - height / 2,
        .right = centerX - width / 2 + width,
        .bottom = centerY - height / 2 + height,
    };
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

[[nodiscard]] std::optional<kb::render::SceneRenderCamera> BuildSelectedCameraOverride(
    const EditorSceneContext& sceneContext,
    std::uint32_t renderWidth,
    std::uint32_t renderHeight) {
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!selected.IsValid()) {
        return std::nullopt;
    }

    const kb::scene::CameraComponent* camera = sceneContext.Scene().Components().Cameras().TryGet(selected);
    const kb::scene::TransformComponent* transform = sceneContext.Scene().Transforms().TryGet(selected);
    if (camera == nullptr || transform == nullptr) {
        return std::nullopt;
    }

    const kb::scene::Vec3 position = transform->worldDirty ? transform->localPosition : transform->worldPosition;
    const kb::scene::Quat rotation = transform->worldDirty ? transform->localRotation : transform->worldRotation;
    return BuildCamera(position, rotation, *camera, renderWidth, renderHeight);
}

[[nodiscard]] std::optional<kb::render::SceneRenderCamera> BuildCameraOverride(
    const EditorSceneContext& sceneContext,
    EditorViewportCameraMode mode,
    std::uint32_t renderWidth,
    std::uint32_t renderHeight) {
    switch (mode) {
    case EditorViewportCameraMode::GameCamera:
        return std::nullopt;
    case EditorViewportCameraMode::EditorCamera:
        return BuildEditorCamera(renderWidth, renderHeight);
    case EditorViewportCameraMode::OverrideCamera:
        if (std::optional<kb::render::SceneRenderCamera> selected = BuildSelectedCameraOverride(sceneContext, renderWidth, renderHeight); selected.has_value()) {
            return selected;
        }
        return BuildEditorCamera(renderWidth, renderHeight);
    }
    return std::nullopt;
}

void DrawSafeAreaOverlay(HDC dc, const RECT& destination, const EditorViewportProfile& profile, const EditorTheme& theme) {
    if (!profile.devicePreview || profile.width == 0U || profile.height == 0U) {
        return;
    }

    const double scaleX = static_cast<double>(RectWidth(destination)) / static_cast<double>(profile.width);
    const double scaleY = static_cast<double>(RectHeight(destination)) / static_cast<double>(profile.height);
    RECT safe = destination;
    safe.left += static_cast<LONG>(std::lround(static_cast<double>(profile.safeArea.left) * scaleX));
    safe.top += static_cast<LONG>(std::lround(static_cast<double>(profile.safeArea.top) * scaleY));
    safe.right -= static_cast<LONG>(std::lround(static_cast<double>(profile.safeArea.right) * scaleX));
    safe.bottom -= static_cast<LONG>(std::lround(static_cast<double>(profile.safeArea.bottom) * scaleY));
    if (safe.left >= safe.right || safe.top >= safe.bottom) {
        return;
    }

    GdiDrawing::FillRectAlpha(dc, destination, GdiDrawing::ToColorRef(theme.background), 34);
    HPEN pen = CreatePen(PS_SOLID, 1, GdiDrawing::ToColorRef(theme.accent));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, safe.left, safe.top, safe.right, safe.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

} // namespace

void PanelContentRenderer::Paint(HDC dc, const RECT& content, const RECT& panelFrame, const RECT& overlayBounds, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, bool floating, EditorSceneBgfxViewport* sceneViewport, HWND sceneViewportHost) const {
    static_cast<void>(panelFrame);
    static_cast<void>(metrics);

    switch (panel.kind) {
    case DockPanelKind::Hierarchy:
        HierarchyPanelRenderer{}.Paint(dc, content, theme, sceneContext);
        break;
    case DockPanelKind::Inspector:
        InspectorPanelRenderer{}.Paint(dc, content, theme, sceneContext);
        break;
    case DockPanelKind::Assets:
        ProjectFilesPanelRenderer{}.Paint(dc, content, overlayBounds, theme, sceneContext);
        break;
    case DockPanelKind::Console:
        GdiDrawing::DrawTextBlock(
            dc,
            content,
            floating ? "[layout] Floating console host\n[info] Awaiting editor events" : "[info] Native window initialized\n[layout] Dock model ready\n[layout] Floating host ready",
            GdiDrawing::ToColorRef(theme.textDisabled));
        break;
    case DockPanelKind::Scene:
        if (sceneViewport != nullptr) {
            const EditorViewportPreviewState& viewportState = sceneContext.ViewportPreview(panel.id);
            SceneViewportToolbarRenderer::Paint(dc, content, theme, viewportState);
            const SceneViewportToolbarRects sceneRects = SceneViewportToolbarRenderer::Resolve(content);
            GdiDrawing::FillRectColor(dc, sceneRects.renderArea, GdiDrawing::ToColorRef(theme.background));
            const EditorViewportProfile profile = viewportState.Profile();
            const std::uint32_t renderWidth = viewportState.RenderWidthForPanel(RectWidth(sceneRects.renderArea));
            const std::uint32_t renderHeight = viewportState.RenderHeightForPanel(RectHeight(sceneRects.renderArea));
            const EditorSceneBgfxViewport::PresentSettings settings{
                .renderWidth = renderWidth,
                .renderHeight = renderHeight,
                .fitMode = viewportState.FitMode(),
                .safeArea = profile.safeArea,
                .cameraOverride = BuildCameraOverride(sceneContext, viewportState.CameraMode(), renderWidth, renderHeight),
                .drawSafeArea = profile.devicePreview,
            };
            if (sceneViewportHost != nullptr) {
                sceneViewport->Present(dc, sceneViewportHost, sceneRects.renderArea, sceneContext.Scene(), theme, settings);
            } else {
                sceneViewport->Present(dc, sceneRects.renderArea, sceneContext.Scene(), theme, settings);
            }
            const RECT destination = CenteredRectFor(sceneRects.renderArea, renderWidth, renderHeight, viewportState.FitMode());
            DrawSafeAreaOverlay(dc, destination, profile, theme);
        }
        break;
    case DockPanelKind::Generic:
    default:
        break;
    }
}

} // namespace kb::editor

#endif
