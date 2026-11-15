#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorOverlayPopupWindow.hpp"

#include <string>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

// The full title of a tab whose title is cut short, under the tab the pointer rests on. Tabs sit
// above the native viewports, so the tooltip lives in a popup above the editor window instead of
// on the window itself. The pointer never lands on it for long: any mouse message it gets goes
// to the editor window, which hides it once the pointer is off the tab.
class DockTabTooltipOverlayWindow final
#if defined(_WIN32)
    : private EditorOverlayPopupWindow::Client
#endif
{
public:
#if defined(_WIN32)
    static constexpr const wchar_t* ClassName = L"KBEditorDockTabTooltipOverlay";

    DockTabTooltipOverlayWindow() noexcept;
    ~DockTabTooltipOverlayWindow() = default;

    DockTabTooltipOverlayWindow(const DockTabTooltipOverlayWindow&) = delete;
    DockTabTooltipOverlayWindow& operator=(const DockTabTooltipOverlayWindow&) = delete;

    // `tab` is the hovered tab in `owner`'s client coordinates.
    void Show(HWND owner, const RECT& tab, std::string_view title, const EditorTheme& theme);
    void Hide() noexcept;
#endif

private:
#if defined(_WIN32)
    void PaintOverlay(HDC dc, const RECT& client) override;
    bool HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

    EditorOverlayPopupWindow popup_;
    std::string title_;
    EditorTheme theme_{};
#endif
};

} // namespace kb::editor
