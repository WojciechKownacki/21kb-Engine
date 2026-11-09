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
class BlendSpace2DEditorPanel final {
public:
    static void Paint(HDC dc,HWND host,DockRect content,const DockPanel& panel,
        const EditorTheme& theme,const EditorSceneContext& context,EditorSceneBgfxViewport* viewport);
    [[nodiscard]] static bool PointerDown(HWND owner,DockRect content,int x,int y,EditorSceneContext& context);
    [[nodiscard]] static bool PointerMove(DockRect content,int x,int y,EditorSceneContext& context);
    [[nodiscard]] static bool PointerUp(HWND owner,EditorSceneContext& context);
    [[nodiscard]] static bool ResolveClose(HWND owner,EditorSceneContext& context);
};
} // namespace kb::editor
#endif
