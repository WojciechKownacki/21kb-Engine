#include "rendering/MainWindowBackBufferPainter.hpp"

#if defined(_WIN32)
#include "rendering/DockDropPreviewOverlayWindow.hpp"
#include "rendering/DockWorkspaceRenderer.hpp"
#include "rendering/EditorDragOverlayRenderer.hpp"
#include "rendering/EditorScriptEditorOverlay.hpp"
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/GdiBackBufferRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectFilesDeleteConfirmOverlayWindow.hpp"
#include "rendering/ProjectFilesFilterMenuOverlayWindow.hpp"
#include "rendering/SceneViewportToolbarDropdownOverlayWindow.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace kb::editor {
namespace {

struct MainWindowPaintContext {
    HWND window = nullptr;
    const EditorDockModel* dockModel = nullptr;
    const EditorTheme* theme = nullptr;
    const EditorMetrics* metrics = nullptr;
    const EditorSceneContext* sceneContext = nullptr;
    const DockDropPreview* preview = nullptr;
    const DockPointerDrag* dockDrag = nullptr;
    const EditorPointerDragState* drag = nullptr;
    const EditorPlayModeState* playMode = nullptr;
    const EditorShellInteractionState* shellInteraction = nullptr;
    EditorSceneBgfxViewport* sceneViewport = nullptr;
};

[[nodiscard]] RECT ToRect(const DockRect& rect) noexcept {
    return RECT{
        .left = rect.x,
        .top = rect.y,
        .right = rect.x + rect.width,
        .bottom = rect.y + rect.height,
    };
}

[[nodiscard]] std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> ResolveViewportLayouts(
    const DockLayout& layout,
    const EditorDockModel& dockModel,
    const EditorSceneContext& sceneContext) {
    std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> layouts;
    for (const DockPanelLayout& panelLayout : layout.panels) {
        if (!panelLayout.active) {
            continue;
        }
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr || panel->kind != DockPanelKind::Scene) {
            continue;
        }
        const RECT content = ToRect(panelLayout.content);
        layouts.push_back(EditorSceneBgfxViewport::HostSurfaceLayout{
            .viewportKey = panelLayout.panelId,
            .bounds = SceneViewportToolbarRenderer::Resolve(content, sceneContext.ViewportPreview(panelLayout.panelId)).renderArea,
        });
    }
    return layouts;
}

[[nodiscard]] std::optional<RECT> ResolveAssetContent(const DockLayout& layout, const EditorDockModel& dockModel) {
    for (const DockPanelLayout& panelLayout : layout.panels) {
        if (!panelLayout.active) {
            continue;
        }
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr || panel->kind != DockPanelKind::Assets) {
            continue;
        }
        return ToRect(panelLayout.content);
    }
    return std::nullopt;
}

struct SceneDropdownContent {
    RECT content{};
    std::uint64_t panelId = 0U;
};

[[nodiscard]] std::optional<SceneDropdownContent> ResolveSceneDropdownContent(
    const DockLayout& layout,
    const EditorDockModel& dockModel,
    const EditorSceneContext& sceneContext) {
    for (const DockPanelLayout& panelLayout : layout.panels) {
        if (!panelLayout.active) {
            continue;
        }
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr || panel->kind != DockPanelKind::Scene) {
            continue;
        }
        if (sceneContext.ViewportPreview(panelLayout.panelId).ToolbarDropdown() == EditorViewportToolbarDropdown::None) {
            continue;
        }
        return SceneDropdownContent{
            .content = ToRect(panelLayout.content),
            .panelId = panelLayout.panelId,
        };
    }
    return std::nullopt;
}

void PaintBackBuffer(const GdiBackBufferPaintContext& paint, void* context) {
    auto* paintContext = static_cast<MainWindowPaintContext*>(context);
    const DockLayout layout = paintContext->dockModel->Queries().BuildLayout(
        paint.width,
        paint.height,
        paintContext->metrics->menuHeight,
        paintContext->metrics->toolbarHeight,
        paintContext->metrics->tabStripHeight,
        paintContext->metrics->tabMinWidth,
        paintContext->metrics->tabWidth,
        paintContext->metrics->splitterSize,
        paintContext->metrics->panelPadding);
    const std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> viewportLayouts =
        ResolveViewportLayouts(layout, *paintContext->dockModel, *paintContext->sceneContext);
    paintContext->sceneViewport->SyncHostSurfaceLayouts(
        paintContext->window,
        std::span<const EditorSceneBgfxViewport::HostSurfaceLayout>{viewportLayouts.data(), viewportLayouts.size()});

    // Hide the script editor's child text control unless its panel is the active
    // tab, so it does not linger over whatever shares its dock leaf.
    const bool scriptEditorActive = std::ranges::any_of(layout.panels, [&](const DockPanelLayout& panelLayout) {
        if (!panelLayout.active) {
            return false;
        }
        const DockPanel* panel = paintContext->dockModel->Queries().FindPanel(panelLayout.panelId);
        return panel != nullptr && panel->kind == DockPanelKind::ScriptEditor;
    });
    if (!scriptEditorActive) {
        EditorScriptEditorOverlay::Hide(paintContext->window);
    }

    EditorSurfacePainter::Fill(paint.dc, paint.client, *paintContext->theme, EditorSurfaceKind::AppBackground);
    SetBkMode(paint.dc, TRANSPARENT);
    DockWorkspaceRenderer{}.Paint(
        paintContext->window,
        paint.dc,
        paint.width,
        paint.height,
        *paintContext->dockModel,
        *paintContext->theme,
        *paintContext->metrics,
        *paintContext->sceneContext,
        nullptr,
        paintContext->dockDrag,
        *paintContext->playMode,
        *paintContext->shellInteraction,
        nullptr);
    EditorDragOverlayRenderer{}.Paint(paint.dc, *paintContext->drag, *paintContext->theme, *paintContext->sceneContext);
}

[[nodiscard]] DockDropPreviewOverlayWindow& MainDropPreviewOverlay() {
    static DockDropPreviewOverlayWindow overlay;
    return overlay;
}

[[nodiscard]] ProjectFilesDeleteConfirmOverlayWindow& MainDeleteConfirmOverlay() {
    static ProjectFilesDeleteConfirmOverlayWindow overlay;
    return overlay;
}

[[nodiscard]] ProjectFilesFilterMenuOverlayWindow& MainFilterMenuOverlay() {
    static ProjectFilesFilterMenuOverlayWindow overlay;
    return overlay;
}

[[nodiscard]] SceneViewportToolbarDropdownOverlayWindow& MainSceneToolbarDropdownOverlay() {
    static SceneViewportToolbarDropdownOverlayWindow overlay;
    return overlay;
}

} // namespace

void MainWindowBackBufferPainter::Paint(HWND window, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, const DockDropPreview* preview, const DockPointerDrag* dockDrag, const EditorPointerDragState& drag, const EditorRenderBackendSettings& renderBackendSettings, const EditorPlayModeState& playMode, const EditorShellInteractionState& shellInteraction, EditorSceneBgfxViewport& sceneViewport) {
    static_cast<void>(renderBackendSettings);
    MainWindowPaintContext context{
        .window = window,
        .dockModel = &dockModel,
        .theme = &theme,
        .metrics = &metrics,
        .sceneContext = &sceneContext,
        .preview = preview,
        .dockDrag = dockDrag,
        .drag = &drag,
        .playMode = &playMode,
        .shellInteraction = &shellInteraction,
        .sceneViewport = &sceneViewport,
    };
    GdiBackBufferRenderer::Paint(window, &PaintBackBuffer, &context);
    if (preview != nullptr) {
        MainDropPreviewOverlay().Show(window, *preview, theme);
    } else {
        MainDropPreviewOverlay().Hide();
    }
    if (sceneContext.AssetBrowser().IsDeleteConfirmOpen()) {
        MainDeleteConfirmOverlay().Show(window, theme, sceneContext);
    } else {
        MainDeleteConfirmOverlay().Hide();
    }
    const DockLayout layout = dockModel.Queries().BuildLayout(
        [] (HWND target) {
            RECT client{};
            GetClientRect(target, &client);
            return client.right - client.left;
        }(window),
        [] (HWND target) {
            RECT client{};
            GetClientRect(target, &client);
            return client.bottom - client.top;
        }(window),
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize,
        metrics.panelPadding);
    if (sceneContext.AssetBrowser().IsFilterMenuOpen()) {
        if (const std::optional<RECT> assetContent = ResolveAssetContent(layout, dockModel); assetContent.has_value()) {
            MainFilterMenuOverlay().Show(window, *assetContent, theme, sceneContext);
        } else {
            MainFilterMenuOverlay().Hide();
        }
    } else {
        MainFilterMenuOverlay().Hide();
    }
    if (const std::optional<SceneDropdownContent> sceneDropdown = ResolveSceneDropdownContent(layout, dockModel, sceneContext); sceneDropdown.has_value()) {
        MainSceneToolbarDropdownOverlay().Show(window, sceneDropdown->content, sceneDropdown->panelId, theme, sceneContext);
    } else {
        MainSceneToolbarDropdownOverlay().Hide();
    }
}

} // namespace kb::editor

#endif
