#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

// Hosts the custom, VS Code-style code editor window for the Script Editor panel,
// overlaid on the panel's body rect: virtualized rendering (only visible lines
// drawn), Lua syntax highlighting, line-number gutter and a dark scrollbar. Loads
// the open script on change, saves it on Ctrl+S, and reports unsaved edits.
class EditorScriptEditorOverlay {
public:
#if defined(_WIN32)
    EditorScriptEditorOverlay() = delete;

    static void Sync(HWND parent, const RECT& content, const EditorSceneContext& sceneContext);
    static void Hide(HWND parent) noexcept;

    // True when the control's text differs from what is saved on disk.
    [[nodiscard]] static bool IsDirty(HWND parent);
#endif
};

} // namespace kb::editor
