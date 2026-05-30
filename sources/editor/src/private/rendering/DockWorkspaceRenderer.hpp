#pragma once

#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockWorkspaceRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, int width, int height, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, const DockDropPreview* preview) const;
#endif

private:
#if defined(_WIN32)
    static void PaintSplitters(HDC dc, const DockLayout& layout, const EditorTheme& theme);
    static void PaintLeaves(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme);
    static void PaintTabs(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme);
    static void PaintActivePanelContent(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext);
#endif
};

} // namespace kb::editor
