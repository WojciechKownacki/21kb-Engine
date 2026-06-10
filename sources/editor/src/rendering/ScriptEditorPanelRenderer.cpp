#include "rendering/ScriptEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <string>

namespace kb::editor {
namespace {

constexpr int kHeaderHeight = 34;

void DrawText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize = 12, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, static_cast<int>(flags | DT_NOPREFIX));
}

} // namespace

RECT ScriptEditorPanelRenderer::BodyRect(const RECT& content) noexcept {
    return RECT{ content.left, content.top + kHeaderHeight, content.right, content.bottom };
}

void ScriptEditorPanelRenderer::Paint(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    bool dirty) const {
    static_cast<void>(theme);
    const EditorScriptEditorState& script = sceneContext.ScriptEditor();

    GdiDrawing::FillRectColor(dc, content, RGB(20, 22, 24));

    const RECT header{ content.left, content.top, content.right, content.top + kHeaderHeight };
    GdiDrawing::FillRectColor(dc, header, RGB(32, 35, 39));
    GdiDrawing::FillRectColor(dc, RECT{ header.left, header.bottom - 1, header.right, header.bottom }, RGB(13, 14, 16));

    if (!script.IsOpen()) {
        DrawText(dc, RECT{ header.left + 12, header.top, header.right - 12, header.bottom }, "Script Editor", RGB(176, 184, 194), 12, FW_SEMIBOLD);
        DrawText(dc, BodyRect(content), "Double-click a .lua file in Project Files to edit it.", RGB(110, 118, 130), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    const std::string title = (dirty ? "\xE2\x97\x8F " : "") + script.Title(); // U+25CF bullet marks unsaved edits.
    DrawText(dc, RECT{ header.left + 12, header.top, header.right - 110, header.bottom }, title.c_str(), dirty ? RGB(226, 196, 120) : RGB(226, 230, 235), 12, FW_SEMIBOLD);
    DrawText(dc, RECT{ header.right - 104, header.top, header.right - 12, header.bottom }, "Ctrl+S to save", RGB(120, 128, 140), 11, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

} // namespace kb::editor

#endif
