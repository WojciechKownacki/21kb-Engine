#pragma once

#include "rendering/EditorPanelContentResolver.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

class EditorDockModel;
class EditorFloatingWindowManager;
struct EditorMetrics;

#if defined(_WIN32)
struct EditorPanelPointerHitContext {
    std::optional<RECT> assetContent;
    std::optional<RECT> inspectorContent;
    std::optional<RECT> consoleContent;
    std::optional<RECT> hierarchyContent;
    std::optional<EditorResolvedPanelContent> sceneContent;
    bool inAssetPanel = false;
    bool inInspectorPanel = false;
    bool inConsolePanel = false;
    bool inHierarchyPanel = false;
};

class EditorPanelPointerHitContextResolver {
public:
    EditorPanelPointerHitContextResolver() = delete;

    [[nodiscard]] static EditorPanelPointerHitContext Resolve(
        HWND messageWindow,
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        int x,
        int y);
};
#endif

} // namespace kb::editor
