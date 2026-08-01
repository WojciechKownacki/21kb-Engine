#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace kb::editor {

class EditorDockModel;
class EditorFloatingWindowManager;
class EditorSceneContext;
struct EditorMetrics;

class EditorTerrainViewportInteraction final {
public:
    EditorTerrainViewportInteraction() = delete;
    [[nodiscard]] static bool UpdateHover(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext);
    [[nodiscard]] static bool Stamp(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        bool beginStroke);
};

} // namespace kb::editor
#endif
