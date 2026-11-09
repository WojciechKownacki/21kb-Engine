#pragma once
#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <optional>
#include <string_view>
namespace kb::editor {
class EditorSceneContext;
class GameplayGraphEditorPanel final {
public:
    static void Paint(HDC dc, DockRect content, const EditorTheme& theme, const EditorSceneContext& context);
    [[nodiscard]] static bool PointerDown(HWND owner, DockRect content, int x, int y, EditorSceneContext& context);
    [[nodiscard]] static bool PointerMove(DockRect content, int x, int y, EditorSceneContext& context);
    static void PointerUp(HWND owner, EditorSceneContext& context);
    [[nodiscard]] static bool ResolveClose(HWND owner, EditorSceneContext& context);
    [[nodiscard]] static std::optional<POINT> NodePoint(DockRect content, const EditorSceneContext& context, std::uint32_t node,
        std::string_view pin = {}, bool output = false);
    [[nodiscard]] static std::optional<POINT> ButtonPoint(DockRect content, std::string_view label);
};
} // namespace kb::editor
#endif
