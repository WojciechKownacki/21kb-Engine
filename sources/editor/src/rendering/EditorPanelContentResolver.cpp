#include "rendering/EditorPanelContentResolver.hpp"

#if defined(_WIN32)
#include "rendering/FloatingPanelGeometry.hpp"
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] RECT IntersectRectOrEmpty(const RECT& a, const RECT& b) noexcept {
    RECT clipped{};
    if (IntersectRect(&clipped, &a, &b) == 0) {
        return {};
    }
    return clipped;
}

[[nodiscard]] DockLayout BuildLayout(HWND window, const EditorDockModel& dockModel, const EditorMetrics& metrics) {
    RECT client{};
    GetClientRect(window, &client);
    return dockModel.Queries().BuildLayout(
        client.right - client.left,
        client.bottom - client.top,
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize);
}

[[nodiscard]] std::optional<EditorResolvedPanelContent> ResolveFloatingPanel(
    DockPanelKind kind,
    HWND sourceWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    const std::uint32_t panelId = floatingWindows.Queries().PanelId(sourceWindow);
    const DockPanel* panel = dockModel.Queries().FindPanel(panelId);
    if (panel == nullptr || panel->kind != kind) {
        return std::nullopt;
    }
    RECT client{};
    GetClientRect(sourceWindow, &client);
    return EditorResolvedPanelContent{
        .content = FloatingPanelGeometry::Content(client, metrics.tabStripHeight),
        .panelId = panelId,
    };
}

[[nodiscard]] std::optional<EditorResolvedPanelContent> ResolveDockedPanel(
    DockPanelKind kind,
    const DockLayout& layout,
    const EditorDockModel& dockModel) {
    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel != nullptr && panelLayout.active && panel->kind == kind) {
            return EditorResolvedPanelContent{
                .content = IntersectRectOrEmpty(GdiDrawing::ToRect(panelLayout.content), GdiDrawing::ToRect(panelLayout.contentClip)),
                .panelId = panelLayout.panelId,
            };
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<RECT> EditorPanelContentResolver::Resolve(
    DockPanelKind kind,
    HWND sourceWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    const std::optional<EditorResolvedPanelContent> resolved = ResolvePanel(kind, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    return resolved.has_value() ? std::optional<RECT>{ resolved->content } : std::nullopt;
}

std::optional<RECT> EditorPanelContentResolver::Resolve(
    DockPanelKind kind,
    const DockLayout& mainLayout,
    HWND sourceWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    const std::optional<EditorResolvedPanelContent> resolved = ResolvePanel(kind, mainLayout, sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    return resolved.has_value() ? std::optional<RECT>{ resolved->content } : std::nullopt;
}

std::optional<EditorResolvedPanelContent> EditorPanelContentResolver::ResolvePanel(
    DockPanelKind kind,
    HWND sourceWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    if (sourceWindow == nullptr || mainWindow == nullptr) {
        return std::nullopt;
    }
    if (sourceWindow != mainWindow) {
        return ResolveFloatingPanel(kind, sourceWindow, dockModel, floatingWindows, metrics);
    }
    return ResolveDockedPanel(kind, BuildLayout(mainWindow, dockModel, metrics), dockModel);
}

std::optional<EditorResolvedPanelContent> EditorPanelContentResolver::ResolvePanel(
    DockPanelKind kind,
    const DockLayout& mainLayout,
    HWND sourceWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    if (sourceWindow == nullptr || mainWindow == nullptr) {
        return std::nullopt;
    }
    if (sourceWindow != mainWindow) {
        return ResolveFloatingPanel(kind, sourceWindow, dockModel, floatingWindows, metrics);
    }
    return ResolveDockedPanel(kind, mainLayout, dockModel);
}

} // namespace kb::editor

#endif
