#pragma once

#include "engine/assets/AssetId.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorDockModel;
class EditorFloatingWindowManager;
struct EditorMetrics;
class EditorSceneContext;

class EditorMeshAssetInspectorDropHandler {
public:
    EditorMeshAssetInspectorDropHandler() = delete;

#if defined(_WIN32)
    [[nodiscard]] static bool Drop(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        kb::assets::AssetId assetId);
#endif
};

} // namespace kb::editor
