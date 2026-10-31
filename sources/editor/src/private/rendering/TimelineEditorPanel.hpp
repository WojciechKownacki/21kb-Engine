#pragma once
#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
namespace kb::editor {
class EditorSceneContext;
class EditorSceneBgfxViewport;
class TimelineEditorPanel {
public:
    static void Paint(HDC dc, HWND host, DockRect content, const DockPanel& panel,
        const EditorTheme& theme, const EditorSceneContext& context, EditorSceneBgfxViewport* viewport);
    [[nodiscard]] static bool Pointer(HWND owner, DockRect content, int x, int y, EditorSceneContext& context);
    [[nodiscard]] static bool ResolveClose(HWND owner, EditorSceneContext& context);
};
}
#endif
