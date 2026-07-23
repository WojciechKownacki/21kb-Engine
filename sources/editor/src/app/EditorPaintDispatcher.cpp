#include <array>
#include "app/EditorPaintDispatcher.hpp"

#if defined(_WIN32)
#include "app/EditorWindowResizeInteraction.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/MaterialPreviewViewportKeys.hpp"
#include "rendering/MaterialPreviewRenderPolicy.hpp"
#include "rendering/ScenePanelContentRenderer.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

#include <bx/math.h>

namespace kb::editor {
namespace {

[[nodiscard]] RECT ToRect(const DockRect& rect) noexcept {
    return RECT{
        .left = rect.x,
        .top = rect.y,
        .right = rect.x + rect.width,
        .bottom = rect.y + rect.height,
    };
}

[[nodiscard]] RECT IntersectRectOrEmpty(const RECT& a, const RECT& b) noexcept {
    RECT clipped{};
    if (IntersectRect(&clipped, &a, &b) == 0) {
        return {};
    }
    return clipped;
}

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
}

[[nodiscard]] float Aspect(std::uint32_t width, std::uint32_t height) noexcept {
    return height == 0U ? 1.0F : static_cast<float>(std::max(1U, width)) / static_cast<float>(height);
}

[[nodiscard]] kb::render::SceneRenderCamera BuildMaterialPreviewCamera(
    std::uint32_t renderWidth,
    std::uint32_t renderHeight,
    const EditorMaterialPreviewSceneSettings& settings) noexcept {
    kb::render::SceneRenderCamera camera{};
    const std::array<float, 3U> eye = settings.CameraEye();
    bx::mtxLookAt(camera.view.data(), bx::Vec3{eye[0], eye[1], eye[2]}, bx::Vec3{0.0F, 0.0F, 0.0F}, bx::Vec3{0.0F, 1.0F, 0.0F});
    kb::render::SceneDepthPolicy::MakePerspective(
        camera.projection.data(),
        settings.verticalFovDegrees,
        Aspect(renderWidth, renderHeight),
        0.05F,
        50.0F,
        kb::render::SceneDepthPolicy::HomogeneousDepth());
    return camera;
}

[[nodiscard]] kb::render::SceneRenderLightingConfig BuildMaterialPreviewLightingConfig(
    const EditorMaterialPreviewSceneSettings& settings,
    kb::project::ProjectSceneLightingPath projectLightingPath) noexcept {
    return MaterialPreviewRenderPolicy::NeutralPbrLightingConfig(settings, projectLightingPath);
}

[[nodiscard]] EditorSceneBgfxViewport::PresentSettings BuildMaterialPreviewSettings(EditorSceneContext& sceneContext, const RECT& previewRect, std::uint64_t viewportKey) {
    const std::uint32_t renderWidth = std::max<std::uint32_t>(1U, RectWidth(previewRect));
    const std::uint32_t renderHeight = std::max<std::uint32_t>(1U, RectHeight(previewRect));
    const EditorMaterialPreviewSceneSettings& previewSettings = sceneContext.MaterialPreviewSceneSettings();
    return EditorSceneBgfxViewport::PresentSettings{
        .renderWidth = renderWidth,
        .renderHeight = renderHeight,
        .fitMode = EditorViewportFitMode::Fit,
        .cameraOverride = BuildMaterialPreviewCamera(renderWidth, renderHeight, previewSettings),
        .viewportKey = viewportKey,
        .editorSceneOverlaysEnabled = false,
        .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueAndTransparent,
        .lightingConfig = BuildMaterialPreviewLightingConfig(previewSettings, sceneContext.Project().sceneLightingPath),
        .materialGraphContext = kb::render::RenderMaterialGraphBuildContext{
            .qualityLevel = previewSettings.qualityLevel,
            .variantUsage = sceneContext.MaterialPreviewNodePreviewEnabled()
                ? kb::render::RenderMaterialGraphVariantUsage::NodePreview
                : kb::render::RenderMaterialGraphVariantUsage::Preview,
        },
        .postProcessSettings = MaterialPreviewRenderPolicy::StableExposurePostProcessSettings(previewSettings),
        .shadowPassEnabled = false,
        .postProcessEnabled = previewSettings.postProcessEnabled && !previewSettings.normalDebugView,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .gpuDrivenRuntimeDispatchEnabled = false,
        .drawSafeArea = false,
        .sceneRevision = sceneContext.MaterialPreviewRevision(),
        .sceneDirtyBaseRevision = sceneContext.MaterialPreviewRevision(),
        .sceneFullSyncRequired = true,
    };
}

[[nodiscard]] const kb::assets::AssetMetadata* MaterialMetadataForAsset(const EditorSceneContext& sceneContext, kb::assets::AssetId assetId) noexcept {
    if (!assetId.IsValid()) {
        return nullptr;
    }
    const kb::assets::AssetMetadata* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(assetId);
    return metadata != nullptr && (metadata->type == "RenderMaterial" || metadata->type == "RenderMaterialInstance") ? metadata : nullptr;
}

[[nodiscard]] bool PresentScenePanel(
    EditorSceneBgfxViewport& sceneViewport,
    HWND host,
    const DockPanel& panel,
    const RECT& content,
    EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings) {
    if (host == nullptr || IsWindow(host) == 0 || IsWindowVisible(host) == 0) {
        return false;
    }

    ScenePanelContentRenderer::PresentViewport(sceneViewport, host, content, panel, sceneContext, renderBackendSettings);
    return true;
}

[[nodiscard]] bool PresentColdMaterialPreview(
    EditorSceneBgfxViewport& sceneViewport,
    HWND host,
    EditorSceneContext& sceneContext,
    std::uint64_t viewportKey,
    const std::optional<RECT>& preview,
    bool forcePresent) {
    if (!preview.has_value() || (!forcePresent && sceneViewport.IsHostSurfaceVisible(host, viewportKey))) {
        return false;
    }
    const kb::assets::AssetMetadata* metadata = MaterialMetadataForAsset(
        sceneContext,
        viewportKey == kMaterialEditorPreviewViewportKey ? sceneContext.MaterialEditor().OpenAssetId() : sceneContext.AssetBrowser().InspectorAsset());
    if (metadata == nullptr) {
        return false;
    }

    const kb::scene::Scene& previewScene = sceneContext.MaterialPreviewScene(metadata->id);
    const EditorSceneBgfxViewport::PresentSettings settings = BuildMaterialPreviewSettings(sceneContext, *preview, viewportKey);
    sceneViewport.Present(host, *preview, previewScene, settings);
    return true;
}

void AppendMaterialPreviewLayout(
    std::vector<EditorSceneBgfxViewport::HostSurfaceLayout>& layouts,
    std::uint64_t viewportKey,
    const std::optional<RECT>& preview) {
    if (preview.has_value() && RectWidth(*preview) > 0U && RectHeight(*preview) > 0U) {
        layouts.push_back(EditorSceneBgfxViewport::HostSurfaceLayout{
            .viewportKey = viewportKey,
            .bounds = *preview,
        });
    }
}

[[nodiscard]] std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> ResolvePaintHostSurfaceLayouts(
    HWND paintWindow,
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> layouts;
    if (paintWindow == mainWindow) {
        RECT client{};
        GetClientRect(mainWindow, &client);
        const DockLayout layout = dockModel.Queries().BuildLayout(
            client.right - client.left,
            client.bottom - client.top,
            metrics.menuHeight,
            metrics.toolbarHeight,
            metrics.tabStripHeight,
            metrics.tabMinWidth,
            metrics.tabWidth,
            metrics.splitterSize,
            metrics.panelPadding);
        for (const DockPanelLayout& panelLayout : layout.panels) {
            if (!panelLayout.active) {
                continue;
            }
            const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
            if (panel == nullptr || panel->kind != DockPanelKind::Scene) {
                continue;
            }
            const RECT content = IntersectRectOrEmpty(ToRect(panelLayout.content), ToRect(panelLayout.contentClip));
            if (RectWidth(content) == 0U || RectHeight(content) == 0U) {
                continue;
            }
            layouts.push_back(EditorSceneBgfxViewport::HostSurfaceLayout{
                .viewportKey = panelLayout.panelId,
                .bounds = SceneViewportToolbarRenderer::Resolve(content, sceneContext.ViewportPreview(panelLayout.panelId)).renderArea,
            });
        }
    } else {
        const DockPanel* panel = dockModel.Queries().FindPanel(floatingWindows.Queries().PanelId(paintWindow));
        if (panel != nullptr && panel->kind == DockPanelKind::Scene) {
            RECT content{};
            GetClientRect(paintWindow, &content);
            content.top += metrics.floatingChromeHeight;
            layouts.push_back(EditorSceneBgfxViewport::HostSurfaceLayout{
                .viewportKey = panel->id,
                .bounds = SceneViewportToolbarRenderer::Resolve(content, sceneContext.ViewportPreview(panel->id)).renderArea,
            });
        }
    }

    const std::optional<RECT> inspector = EditorPanelContentResolver::Resolve(
        DockPanelKind::Inspector,
        paintWindow,
        mainWindow,
        dockModel,
        floatingWindows,
        metrics);
    const std::optional<RECT> materialEditor = EditorPanelContentResolver::Resolve(
        DockPanelKind::MaterialEditor,
        paintWindow,
        mainWindow,
        dockModel,
        floatingWindows,
        metrics);
    AppendMaterialPreviewLayout(
        layouts,
        kInspectorMaterialPreviewViewportKey,
        inspector.has_value() ? InspectorPanelRenderer::MaterialPreviewRect(*inspector, sceneContext) : std::nullopt);
    AppendMaterialPreviewLayout(
        layouts,
        kMaterialEditorPreviewViewportKey,
        materialEditor.has_value() ? MaterialEditorPanelRenderer::MaterialPreviewRect(*materialEditor, sceneContext) : std::nullopt);
    return layouts;
}

[[nodiscard]] bool PresentPaintHostViewports(
    HWND paintWindow,
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport& sceneViewport) {
    if (paintWindow == nullptr || IsWindow(paintWindow) == 0 || IsWindowVisible(paintWindow) == 0) {
        return false;
    }

    const std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> layouts =
        ResolvePaintHostSurfaceLayouts(paintWindow, mainWindow, dockModel, floatingWindows, metrics, sceneContext);

    bool scenePresented = false;
    sceneViewport.BeginPaintLayout(paintWindow);
    sceneViewport.SyncHostSurfaceLayouts(
        paintWindow,
        std::span<const EditorSceneBgfxViewport::HostSurfaceLayout>{layouts.data(), layouts.size()});
    const bool forcePreviewPresent = EditorWindowResizeInteraction::IsWindowResizing(paintWindow) || sceneViewport.PresentRequested();
    const std::optional<RECT> inspector = EditorPanelContentResolver::Resolve(
        DockPanelKind::Inspector,
        paintWindow,
        mainWindow,
        dockModel,
        floatingWindows,
        metrics);
    const std::optional<RECT> materialEditor = EditorPanelContentResolver::Resolve(
        DockPanelKind::MaterialEditor,
        paintWindow,
        mainWindow,
        dockModel,
        floatingWindows,
        metrics);
    const bool coldPreviewPresented =
        PresentColdMaterialPreview(
            sceneViewport,
            paintWindow,
            sceneContext,
            kInspectorMaterialPreviewViewportKey,
            inspector.has_value() ? InspectorPanelRenderer::MaterialPreviewRect(*inspector, sceneContext) : std::nullopt,
            forcePreviewPresent)
        || PresentColdMaterialPreview(
            sceneViewport,
            paintWindow,
            sceneContext,
            kMaterialEditorPreviewViewportKey,
            materialEditor.has_value() ? MaterialEditorPanelRenderer::MaterialPreviewRect(*materialEditor, sceneContext) : std::nullopt,
            forcePreviewPresent);
    if (paintWindow == mainWindow) {
        RECT client{};
        GetClientRect(mainWindow, &client);
        const DockLayout layout = dockModel.Queries().BuildLayout(
            client.right - client.left,
            client.bottom - client.top,
            metrics.menuHeight,
            metrics.toolbarHeight,
            metrics.tabStripHeight,
            metrics.tabMinWidth,
            metrics.tabWidth,
            metrics.splitterSize,
            metrics.panelPadding);
        for (const DockPanelLayout& panelLayout : layout.panels) {
            if (!panelLayout.active) {
                continue;
            }
            const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
            if (panel == nullptr || panel->kind != DockPanelKind::Scene) {
                continue;
            }
            const RECT content = IntersectRectOrEmpty(ToRect(panelLayout.content), ToRect(panelLayout.contentClip));
            if (RectWidth(content) == 0U || RectHeight(content) == 0U) {
                continue;
            }
            scenePresented = PresentScenePanel(sceneViewport, mainWindow, *panel, content, sceneContext, renderBackendSettings) || scenePresented;
        }
    } else {
        const DockPanel* panel = dockModel.Queries().FindPanel(floatingWindows.Queries().PanelId(paintWindow));
        if (panel != nullptr && panel->kind == DockPanelKind::Scene) {
            RECT content{};
            GetClientRect(paintWindow, &content);
            content.top += metrics.floatingChromeHeight;
            scenePresented = PresentScenePanel(sceneViewport, paintWindow, *panel, content, sceneContext, renderBackendSettings);
        }
    }
    sceneViewport.EndPaintLayout();
    if (scenePresented || coldPreviewPresented) {
        sceneViewport.ClearPresentRequest();
    }
    if (scenePresented) {
        sceneContext.AcknowledgeSceneRenderSubmitted();
    }
    return scenePresented || coldPreviewPresented;
}

} // namespace

EditorPaintDispatcher::EditorPaintDispatcher(
    HWND& mainWindow,
    EditorDockModel& dockModel,
    EditorSceneContext& sceneContext,
    EditorTheme& theme,
    EditorMetrics& metrics,
    EditorGdiRenderer& renderer,
    EditorRenderBackendSettings& renderBackendSettings,
    EditorPlayModeState& playMode,
    EditorShellInteractionState& shellInteraction,
    EditorSceneBgfxViewport& sceneViewport,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorPointerDragState& pointerDrag) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , sceneContext_(sceneContext)
    , theme_(theme)
    , metrics_(metrics)
    , renderer_(renderer)
    , renderBackendSettings_(renderBackendSettings)
    , playMode_(playMode)
    , shellInteraction_(shellInteraction)
    , sceneViewport_(sceneViewport)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , pointerDrag_(pointerDrag) {}

void EditorPaintDispatcher::Paint(HWND paintWindow) const {
    if (paintWindow == nullptr || IsMainWindow(paintWindow)) {
        renderer_.Paint(mainWindow_, dockModel_, theme_, metrics_, sceneContext_, dockController_.DropPreview(), dockController_.ActiveDrag(), pointerDrag_, renderBackendSettings_, playMode_, shellInteraction_, sceneViewport_);
        const DockPointerDrag* activeDrag = dockController_.ActiveDrag();
        const bool draggingSplitter = activeDrag != nullptr && activeDrag->kind == DockHitKind::Splitter;
        if (EditorWindowResizeInteraction::IsWindowResizing(mainWindow_) || draggingSplitter) {
            static_cast<void>(PresentPaintHostViewports(mainWindow_, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, renderBackendSettings_, sceneViewport_));
        }
        return;
    }

    if (const DockPanel* panel = dockModel_.Queries().FindPanel(floatingWindows_.Queries().PanelId(paintWindow)); panel != nullptr) {
        renderer_.PaintFloating(paintWindow, *panel, theme_, metrics_, sceneContext_, renderBackendSettings_, sceneViewport_);
        if (EditorWindowResizeInteraction::IsWindowResizing(paintWindow)) {
            static_cast<void>(PresentPaintHostViewports(paintWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, renderBackendSettings_, sceneViewport_));
        }
    }
}

bool EditorPaintDispatcher::IsMainWindow(HWND candidate) const noexcept {
    return candidate == mainWindow_;
}

} // namespace kb::editor

#endif
