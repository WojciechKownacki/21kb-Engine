#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneContext;

enum class EditorAssetBrowserDoubleClickResult {
    None,
    BrowserNavigation,
    SceneOpened,
    ScriptEditorOpened,
};

class EditorAssetBrowserDoubleClickHandler {
public:
    EditorAssetBrowserDoubleClickHandler() = delete;

    [[nodiscard]] static EditorAssetBrowserDoubleClickResult HandleDoubleClick(HWND owner, const RECT& content, int x, int y, EditorSceneContext& sceneContext);
};

#endif

} // namespace kb::editor
