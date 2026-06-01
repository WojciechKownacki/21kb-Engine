#pragma once

#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockWorkspaceContentRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport) const;
#endif
};

} // namespace kb::editor
