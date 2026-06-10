#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

// Draws the Script Editor panel chrome (header with the open file name + a
// dirty/save hint, and the dark body). The editable text itself lives in a Win32
// EDIT control overlaid on BodyRect(content) by EditorScriptEditorOverlay.
class ScriptEditorPanelRenderer {
public:
#if defined(_WIN32)
    void Paint(
        HDC dc,
        const RECT& content,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        bool dirty) const;

    // Area the editable text control occupies (below the header strip).
    [[nodiscard]] static RECT BodyRect(const RECT& content) noexcept;
#endif
};

} // namespace kb::editor
