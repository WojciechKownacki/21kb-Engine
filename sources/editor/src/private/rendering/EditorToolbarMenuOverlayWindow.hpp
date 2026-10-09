#pragma once

#include "app/EditorShellInteractionState.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorOverlayPopupWindow.hpp"

#include <optional>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

// The open toolbar menu's dropdown (File, Edit, Layout, Options, World, Help). It
// hangs below the menu bar over the docked panels, scene viewports included, so it
// lives in a popup above the editor window instead of on the window itself. The
// rows are painted by EditorToolbarRenderer::PaintMenuDropdown, and clicks and
// hover are handed to the editor window in its own coordinates, where the toolbar
// pointer handling hit-tests them exactly as it always has.
class EditorToolbarMenuOverlayWindow final
#if defined(_WIN32)
    : private EditorOverlayPopupWindow::Client
#endif
{
public:
#if defined(_WIN32)
    static constexpr const wchar_t* ClassName = L"KBEditorToolbarMenuOverlay";

    EditorToolbarMenuOverlayWindow() noexcept;
    ~EditorToolbarMenuOverlayWindow() = default;

    EditorToolbarMenuOverlayWindow(const EditorToolbarMenuOverlayWindow&) = delete;
    EditorToolbarMenuOverlayWindow& operator=(const EditorToolbarMenuOverlayWindow&) = delete;

    // `menuBar` is the menu bar in `owner`'s client coordinates.
    void Show(HWND owner, const RECT& menuBar, const EditorTheme& theme, const EditorShellInteractionState& interaction);
    void Hide() noexcept;
#endif

private:
#if defined(_WIN32)
    struct RenderedState {
        EditorMenuCommand menu = EditorMenuCommand::None;
        std::optional<int> hoveredRow{};
        int rowCount = 0;
        std::string layoutRows;

        [[nodiscard]] bool operator==(const RenderedState&) const = default;
    };

    [[nodiscard]] static RenderedState Capture(const EditorShellInteractionState& interaction);
    void PaintOverlay(HDC dc, const RECT& client) override;
    bool HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

    EditorOverlayPopupWindow popup_;
    RECT menuBar_{};
    RECT paintBounds_{};
    EditorTheme theme_{};
    const EditorShellInteractionState* interaction_ = nullptr;
    RenderedState rendered_{};
#endif
};

} // namespace kb::editor
