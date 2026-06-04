#pragma once

#include "app/EditorPlayModeState.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#include <array>
#include <optional>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

struct EditorToolbarRects {
#if defined(_WIN32)
    RECT toolbar{};
    RECT playButton{};
    RECT stopButton{};
    RECT pauseButton{};
#endif
};

struct EditorMenuRects {
#if defined(_WIN32)
    RECT menuBar{};
    RECT file{};
    RECT edit{};
    RECT options{};
    RECT help{};
    RECT dropdown{};
    std::array<RECT, 4> dropdownRows{};
#endif
};

class EditorToolbarRenderer {
public:
#if defined(_WIN32)
    [[nodiscard]] static EditorMenuRects ResolveMenu(const RECT& rect, EditorMenuCommand openMenu) noexcept;
    [[nodiscard]] static EditorToolbarRects ResolveToolbar(const RECT& rect) noexcept;
    [[nodiscard]] static EditorMenuCommand HitTestMenu(const EditorMenuRects& rects, int x, int y) noexcept;
    [[nodiscard]] static std::optional<int> HitTestMenuRow(const EditorMenuRects& rects, int x, int y) noexcept;
    [[nodiscard]] static EditorTransportCommand HitTestTransport(const EditorToolbarRects& rects, int x, int y) noexcept;

    void PaintMenu(HDC dc, const RECT& rect, const EditorTheme& theme, const EditorShellInteractionState& interaction) const;
    void PaintToolbar(HDC dc, const RECT& rect, const EditorTheme& theme, const EditorPlayModeState& playMode, const EditorShellInteractionState& interaction) const;
#endif

private:
#if defined(_WIN32)
    void PaintButton(HDC dc, const RECT& rect, const EditorTheme& theme, bool enabled, bool active, bool hovered, bool pressed, COLORREF glow) const;
#endif
};

} // namespace kb::editor
