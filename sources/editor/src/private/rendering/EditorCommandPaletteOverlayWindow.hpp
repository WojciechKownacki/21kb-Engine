#pragma once

#include "app/EditorCommandPaletteModel.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorOverlayPopupWindow.hpp"

#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

// The command palette: a text box with the list under it, centred over the editor window. It sits above the
// native viewports, so it lives in a popup above the window. The popup only paints; keys stay with the editor
// window, and a click or the pointer's movement is handed to the editor window in its own coordinates, where
// the palette's controller hit-tests it against the same layout.
class EditorCommandPaletteOverlayWindow final
#if defined(_WIN32)
    : private EditorOverlayPopupWindow::Client
#endif
{
public:
#if defined(_WIN32)
    static constexpr const wchar_t* ClassName = L"KBEditorCommandPaletteOverlay";

    EditorCommandPaletteOverlayWindow() noexcept;
    ~EditorCommandPaletteOverlayWindow() = default;

    EditorCommandPaletteOverlayWindow(const EditorCommandPaletteOverlayWindow&) = delete;
    EditorCommandPaletteOverlayWindow& operator=(const EditorCommandPaletteOverlayWindow&) = delete;

    void Show(HWND owner, const EditorTheme& theme, const EditorShellInteractionState& interaction);
    void Hide() noexcept;
#endif

private:
#if defined(_WIN32)
    void PaintOverlay(HDC dc, const RECT& client) override;
    bool HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

    EditorOverlayPopupWindow popup_;
    EditorTheme theme_{};
    const EditorCommandPaletteModel* palette_ = nullptr;
    // The layout in the owner window's coordinates.
    EditorCommandPaletteLayout layout_{};
    std::uint64_t paintedRevision_ = 0;
#endif
};

} // namespace kb::editor
