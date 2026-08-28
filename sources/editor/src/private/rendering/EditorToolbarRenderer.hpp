#pragma once

#include "app/EditorPlayModeState.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#include <array>
#include <optional>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

struct EditorToolbarRects {
#if defined(_WIN32)
    RECT toolbar{};
    RECT saveButton{};
    RECT playButton{};
    RECT stopButton{};
    RECT pauseButton{};
#endif
};

struct EditorMenuRects {
#if defined(_WIN32)
    // The Layout menu is as long as the project has layouts, so the rows are a
    // fixed-capacity run with a live count rather than a fixed four.
    static constexpr std::size_t MaximumRows = 16U;

    RECT menuBar{};
    RECT file{};
    RECT edit{};
    RECT layout{};
    RECT options{};
    RECT help{};
    RECT dropdown{};
    std::array<RECT, MaximumRows> dropdownRows{};
    int dropdownRowCount = 0;
#endif
};

class EditorToolbarRenderer {
public:
#if defined(_WIN32)
    [[nodiscard]] static EditorMenuRects ResolveMenu(
        const RECT& rect, EditorMenuCommand openMenu, int rowCount) noexcept;
    [[nodiscard]] static EditorToolbarRects ResolveToolbar(const RECT& rect) noexcept;
    [[nodiscard]] static EditorMenuCommand HitTestMenu(const EditorMenuRects& rects, int x, int y) noexcept;
    [[nodiscard]] static std::optional<int> HitTestMenuRow(const EditorMenuRects& rects, int x, int y) noexcept;
    [[nodiscard]] static EditorTransportCommand HitTestTransport(const EditorToolbarRects& rects, int x, int y) noexcept;
    [[nodiscard]] static bool HitTestSave(const EditorToolbarRects& rects, int x, int y) noexcept;

    void PaintMenu(HDC dc, const RECT& rect, const EditorTheme& theme, const EditorShellInteractionState& interaction) const;
    void PaintToolbar(HDC dc, const RECT& rect, const EditorTheme& theme, const EditorSceneContext& sceneContext, const EditorPlayModeState& playMode, const EditorShellInteractionState& interaction) const;
#endif

private:
#if defined(_WIN32)
    void PaintButton(HDC dc, const RECT& rect, const EditorTheme& theme, bool enabled, bool active, bool hovered, bool pressed, COLORREF glow) const;
    void PaintTextButton(HDC dc, const RECT& rect, const EditorTheme& theme, std::string_view label, bool enabled, bool active, bool hovered, bool pressed, COLORREF glow) const;
#endif
};

} // namespace kb::editor
