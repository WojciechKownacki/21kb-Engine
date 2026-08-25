#include "app/panels/EditorPanelPointerHitContext.hpp"

#if defined(_WIN32)
#include "scene/EditorHierarchyContentResolver.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

EditorPanelPointerHitContext EditorPanelPointerHitContextResolver::Resolve(
    HWND messageWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    int x,
    int y) {
    EditorPanelPointerHitContext context{};
    context.assetContent = EditorPanelContentResolver::Resolve(DockPanelKind::Assets, messageWindow, mainWindow, dockModel, floatingWindows, metrics);
    context.inspectorContent = EditorPanelContentResolver::Resolve(DockPanelKind::Inspector, messageWindow, mainWindow, dockModel, floatingWindows, metrics);
    context.consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow, dockModel, floatingWindows, metrics);
    context.projectSettingsContent = EditorPanelContentResolver::Resolve(DockPanelKind::ProjectSettings, messageWindow, mainWindow, dockModel, floatingWindows, metrics);
    context.editorSettingsContent = EditorPanelContentResolver::Resolve(DockPanelKind::EditorSettings, messageWindow, mainWindow, dockModel, floatingWindows, metrics);
    context.pluginsContent = EditorPanelContentResolver::Resolve(DockPanelKind::Plugins, messageWindow, mainWindow, dockModel, floatingWindows, metrics);
    context.hierarchyContent = EditorHierarchyContentResolver::Resolve(messageWindow, mainWindow, dockModel, floatingWindows, metrics);

    const std::optional<EditorResolvedPanelContent> scenePanelContent =
        EditorPanelContentResolver::ResolvePanel(DockPanelKind::Scene, messageWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (scenePanelContent.has_value() && PointInRect(scenePanelContent->content, x, y)) {
        context.sceneContent = scenePanelContent;
    }

    context.inAssetPanel = context.assetContent.has_value() && PointInRect(*context.assetContent, x, y);
    context.inInspectorPanel = context.inspectorContent.has_value() && PointInRect(*context.inspectorContent, x, y);
    context.inConsolePanel = context.consoleContent.has_value() && PointInRect(*context.consoleContent, x, y);
    context.inProjectSettingsPanel = context.projectSettingsContent.has_value() && PointInRect(*context.projectSettingsContent, x, y);
    context.inEditorSettingsPanel = context.editorSettingsContent.has_value() && PointInRect(*context.editorSettingsContent, x, y);
    context.inPluginsPanel = context.pluginsContent.has_value() && PointInRect(*context.pluginsContent, x, y);
    context.inHierarchyPanel = context.hierarchyContent.has_value() && PointInRect(*context.hierarchyContent, x, y);
    return context;
}

} // namespace kb::editor

#endif
