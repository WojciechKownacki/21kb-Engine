#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// The Packages panel: the packages of the registry and of the open project, buttons to add, update and remove them,
// a check that the installed files are unchanged, and what the last operation did, in plain words.
class PackagesPanel final {
public:
    PackagesPanel() = delete;

    // Where the panel opens: beside the Console and Project Files.
    static constexpr DockArea kOpenArea = DockArea::Bottom;

    static void Paint(HDC dc, const RECT& content, const EditorTheme& theme);
    // A left click at (x, y); true when the panel took it. `owner` hosts the dialog that asks for the registry.
    static bool Pointer(HWND owner, const RECT& content, int x, int y);
    // A turn of the mouse wheel over the list; true when it scrolled.
    static bool Wheel(const RECT& content, int x, int y, int delta);
};

#endif

} // namespace kb::editor
