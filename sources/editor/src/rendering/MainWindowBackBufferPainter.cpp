#include "rendering/MainWindowBackBufferPainter.hpp"

#if defined(_WIN32)
#include "app/EditorWindowResizeInteraction.hpp"
#include "rendering/DockDropPreviewOverlayWindow.hpp"
#include "rendering/DockWorkspaceRenderer.hpp"
#include "rendering/EditorDragOverlayRenderer.hpp"
#include "rendering/EditorHostSurfaceLayoutResolver.hpp"
#include "rendering/EditorScriptEditorOverlay.hpp"
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/GdiBackBufferRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/InspectorAddComponentOverlayWindow.hpp"
#include "rendering/ProjectFilesDeleteConfirmOverlayWindow.hpp"
#include "rendering/ProjectFilesFilterMenuOverlayWindow.hpp"
#include "rendering/SceneViewportToolbarDropdownOverlayWindow.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorTerrainService.hpp"

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
    EditorSceneContext* sceneContext = nullptr;
    const EditorRenderBackendSettings* renderBackendSettings = nullptr;
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

[[nodiscard]] std::optional<RECT> ResolveInspectorContent(const DockLayout& layout, const EditorDockModel& dockModel) {
    for (const DockPanelLayout& panelLayout : layout.panels) {
        if (!panelLayout.active) {
            continue;
        }
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel != nullptr && panel->kind == DockPanelKind::Inspector) {
            return ToRect(panelLayout.content);
        }
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
        const EditorTerrainToolState& terrainTool = EditorTerrainService::ToolState();
        if (sceneContext.ViewportPreview(panelLayout.panelId).ToolbarDropdown() == EditorViewportToolbarDropdown::None &&
            !terrainTool.brushMenuOpen && !terrainTool.brushShapeMenuOpen) {
            continue;
        }
        return SceneDropdownContent{
            .content = ToRect(panelLayout.content),
            .panelId = panelLayout.panelId,
        };
    }
    return std::nullopt;
}

void PaintAutosaveNotification(
    HDC dc,
    const RECT& client,
    const EditorTheme& theme,
    const EditorAutosaveState& autosave) {
    if (!autosave.NotificationVisible()) {
        return;
    }

    constexpr LONG margin = 18;
    constexpr LONG width = 320;
    constexpr LONG height = 46;
    const RECT card{
        .left = std::max(client.left + 8, client.right - margin - width),
        .top = std::max(client.top + 8, client.bottom - margin - height),
        .right = client.right - margin,
        .bottom = client.bottom - margin,
    };
    if (card.right <= card.left || card.bottom <= card.top) {
        return;
    }

    const COLORREF statusColor = autosave.NotificationSucceeded()
        ? GdiDrawing::ToColorRef(theme.accent)
        : RGB(214, 92, 92);
    GdiDrawing::DrawSharpFrame(
        dc,
        card,
        GdiDrawing::ToColorRef(theme.chrome),
        GdiDrawing::ToColorRef(theme.borderChrome));
    GdiDrawing::FillRectColor(
        dc,
        RECT{ card.left, card.top, card.left + 3, card.bottom },
        statusColor);
    const RECT icon{
        card.left + 14,
        card.top + 12,
        card.left + 36,
        card.bottom - 12,
    };
    HeroIconPainter::Draw(
        dc, icon, HeroIconKind::DocumentText, statusColor, 2);
    const RECT text{
        icon.right + 10,
        card.top,
        card.right - 12,
        card.bottom,
    };
    GdiDrawing::DrawTabText(
        dc,
        text,
        autosave.NotificationText().c_str(),
        GdiDrawing::ToColorRef(theme.textPrimary));
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
        paintContext->metrics->splitterSize);
    const std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> viewportLayouts =
        EditorHostSurfaceLayoutResolver::ResolveMainWindow(
            paintContext->window,
            *paintContext->dockModel,
            *paintContext->metrics,
            *paintContext->sceneContext);
    const std::span<const EditorSceneBgfxViewport::HostSurfaceLayout> viewportLayoutSpan{viewportLayouts.data(), viewportLayouts.size()};
    if (EditorWindowResizeInteraction::IsWindowResizing(paintContext->window) || (paintContext->dockDrag != nullptr && paintContext->dockDrag->kind == DockHitKind::Splitter)) {
        paintContext->sceneViewport->SyncHostSurfaceLayoutsForResize(paintContext->window, viewportLayoutSpan);
    } else {
        paintContext->sceneViewport->SyncHostSurfaceLayouts(paintContext->window, viewportLayoutSpan);
    }

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
        paint.dirty,
        paint.width,
        paint.height,
        *paintContext->dockModel,
        *paintContext->theme,
        *paintContext->metrics,
        *paintContext->sceneContext,
        *paintContext->renderBackendSettings,
        nullptr,
        paintContext->dockDrag,
        *paintContext->playMode,
        *paintContext->shellInteraction,
        nullptr);
    EditorDragOverlayRenderer{}.Paint(paint.dc, *paintContext->drag, *paintContext->theme, *paintContext->sceneContext);
    PaintAutosaveNotification(
        paint.dc,
        paint.client,
        *paintContext->theme,
        paintContext->sceneContext->Autosave());
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

[[nodiscard]] InspectorAddComponentOverlayWindow& MainAddComponentOverlay() {
    static InspectorAddComponentOverlayWindow overlay;
    return overlay;
}

} // namespace

void MainWindowBackBufferPainter::HideAllOverlays() noexcept {
    MainDropPreviewOverlay().Hide();
    MainDeleteConfirmOverlay().Hide();
    MainFilterMenuOverlay().Hide();
    MainSceneToolbarDropdownOverlay().Hide();
    MainAddComponentOverlay().Hide();
}

void MainWindowBackBufferPainter::Paint(HWND window, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const DockDropPreview* preview, const DockPointerDrag* dockDrag, const EditorPointerDragState& drag, const EditorRenderBackendSettings& renderBackendSettings, const EditorPlayModeState& playMode, const EditorShellInteractionState& shellInteraction, EditorSceneBgfxViewport& sceneViewport) {
    MainWindowPaintContext context{
        .window = window,
        .dockModel = &dockModel,
        .theme = &theme,
        .metrics = &metrics,
        .sceneContext = &sceneContext,
        .renderBackendSettings = &renderBackendSettings,
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
        metrics.splitterSize);
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
    if (sceneContext.Inspector().IsAddComponentBrowserOpen()) {
        if (const std::optional<RECT> inspectorContent = ResolveInspectorContent(layout, dockModel); inspectorContent.has_value()) {
            MainAddComponentOverlay().Show(window, *inspectorContent, theme, sceneContext);
        } else {
            MainAddComponentOverlay().Hide();
        }
    } else {
        MainAddComponentOverlay().Hide();
    }
}

} // namespace kb::editor

#endif
