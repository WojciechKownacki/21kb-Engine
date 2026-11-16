#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// The Version Control panel: the files of the project that differ from the last saved version, a way to bring one
// file back, and what changed in the open scene (objects added, removed or changed, with the old and new values).
class VersionControlPanel final {
public:
    VersionControlPanel() = delete;

    // Where the panel opens: beside the Console and Project Files.
    static constexpr DockArea kOpenArea = DockArea::Bottom;

    static void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext);
    // A left click at (x, y); true when the panel took it. `owner` hosts the dialog that asks before a file is brought back.
    static bool Pointer(HWND owner, const RECT& content, int x, int y, EditorSceneContext& sceneContext);
    // A turn of the mouse wheel over the panel; true when something scrolled.
    static bool Wheel(const RECT& content, int x, int y, int delta);
};

#endif

} // namespace kb::editor
