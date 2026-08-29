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
    const EditorScriptEditorState& script = sceneContext.ScriptEditor();

    GdiDrawing::FillRectColor(dc, content, GdiDrawing::ToColorRef(theme.background));

    const RECT header{ content.left, content.top, content.right, content.top + kHeaderHeight };
    GdiDrawing::FillRectColor(dc, header, GdiDrawing::ToColorRef(theme.strip));
    GdiDrawing::FillRectColor(dc, RECT{ header.left, header.top, header.left + 3, header.bottom }, GdiDrawing::ToColorRef(theme.accent));
    GdiDrawing::FillRectColor(dc, RECT{ header.left, header.bottom - 1, header.right, header.bottom }, GdiDrawing::ToColorRef(theme.borderChrome));

    if (!script.IsOpen()) {
        DrawText(dc, RECT{ header.left + 12, header.top, header.right - 12, header.bottom }, "Script Editor", GdiDrawing::ToColorRef(theme.textSecondary), 12, FW_SEMIBOLD);
        DrawText(dc, BodyRect(content), "Double-click a .lua file in Project Files to edit it.", GdiDrawing::ToColorRef(theme.textDisabled), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    const std::string title = (dirty ? "\xE2\x97\x8F " : "") + script.Title(); // U+25CF bullet marks unsaved edits.
    DrawText(dc, RECT{ header.left + 12, header.top, header.right - 110, header.bottom }, title.c_str(), dirty ? RGB(226, 196, 120) : GdiDrawing::ToColorRef(theme.textPrimary), 12, FW_SEMIBOLD);
    DrawText(dc, RECT{ header.right - 104, header.top, header.right - 12, header.bottom }, "Ctrl+S to save", GdiDrawing::ToColorRef(theme.textDisabled), 11, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

} // namespace kb::editor

#endif
