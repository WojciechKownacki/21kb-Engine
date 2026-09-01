#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include "rendering/EditorPanelStyle.hpp"

#include <span>

namespace kb::editor {

#if defined(_WIN32)

// Pure geometry of the Build Game panel: a target sidebar on the left with the build
// profiles pinned under it, a target header and a settings column on the right, and a
// status/action footer across the bottom. Kept apart from the renderer so painting and
// hit-testing read the same rectangles without touching a GDI device.
struct BuildGamePanelLayoutRects {
    RECT sidebar{};           // Left column: targets on top, profiles at the bottom.
    RECT platformsCaption{};  // "PLATFORMS".
    RECT platformsList{};     // Target rows, including their two group captions.
    RECT profilesCaption{};   // "BUILD PROFILES".
    RECT profilesList{};      // Profile rows.
    RECT divider{};           // 1px line between sidebar and content.
    RECT header{};            // Selected target card.
    RECT body{};              // Settings column.
    RECT footer{};            // Status text and the build action.
    RECT status{};            // Two stacked status lines, left of the button.
    RECT buildButton{};
};

class BuildGamePanelLayout {
public:
    BuildGamePanelLayout() = delete;

    // Row heights the renderer advances by as it walks the settings column. They are the
    // shared panel metrics, so a Build Game row lines up with an Inspector row.
    static constexpr int kSectionHeaderHeight = panel_style::kSectionHeaderHeight;
    static constexpr int kSectionSpacing = 8;
    static constexpr int kOptionRowHeight = panel_style::kFieldRowHeight;
    static constexpr int kTargetRowHeight = 42;
    static constexpr int kGroupCaptionHeight = 22;
    static constexpr int kProfileRowHeight = 44;

    [[nodiscard]] static BuildGamePanelLayoutRects Resolve(const RECT& content) noexcept;

    // A target row. `index` counts player package rows below the platform caption.
    [[nodiscard]] static RECT TargetRow(const RECT& list, int index) noexcept;
    [[nodiscard]] static RECT TargetGroupCaption(const RECT& list, int index) noexcept;
    [[nodiscard]] static RECT ProfileRow(const RECT& list, int index) noexcept;

    // Rows inside the settings column, stacked from `y` (which the renderer advances).
    [[nodiscard]] static RECT SectionHeaderRow(const RECT& body, int y) noexcept;
    [[nodiscard]] static RECT OptionRow(const RECT& body, int y) noexcept;
    // The value cell of an option row: right of the label, inset so it reads as a field.
    [[nodiscard]] static RECT OptionValueBox(const RECT& row) noexcept;
    [[nodiscard]] static RECT OptionLabel(const RECT& row) noexcept;
    // The square icon box at the left edge of a row or header.
    [[nodiscard]] static RECT IconBox(const RECT& row, int size) noexcept;
    // The disclosure caret, ahead of the section icon on a category bar.
    [[nodiscard]] static RECT CaretBox(const RECT& header) noexcept;

    // How tall the settings column would be if nothing clipped it, and how far it can be
    // scrolled inside `body`. The panel lists more rows than a dock leaf is ever tall.
    [[nodiscard]] static int ContentHeight(std::span<const int> sectionRowCounts) noexcept;
    [[nodiscard]] static int MaxScrollOffset(const RECT& body, int contentHeight) noexcept;
    // Empty when the content fits; otherwise the track and the thumb inside it.
    [[nodiscard]] static RECT ScrollbarTrack(const RECT& body, int contentHeight) noexcept;
    [[nodiscard]] static RECT ScrollbarThumb(const RECT& body, int contentHeight, int scrollOffset) noexcept;
};

#endif

} // namespace kb::editor
