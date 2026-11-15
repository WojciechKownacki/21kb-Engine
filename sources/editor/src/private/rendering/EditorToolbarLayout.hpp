#pragma once

#include "rendering/EditorToolbarRenderer.hpp"
#include "rendering/HeroIconKind.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace kb::editor {

struct EditorMenuDescriptor {
    EditorMenuCommand command = EditorMenuCommand::None;
    int width = 0;
};

class EditorToolbarLayout {
public:
#if defined(_WIN32)
    // The menu titles' font size; a title wider than its slot widens the slot.
    static constexpr int MenuFontSize = 13;
    // A dropdown row's second line, the caption above a group of rows and the rule between groups.
    static constexpr int DropdownDetailFontSize = 11;
    static constexpr int DropdownHeadingHeight = 28;
    static constexpr int DropdownSeparatorHeight = 9;
    [[nodiscard]] static const std::array<EditorMenuDescriptor, 6>& MenuDescriptors() noexcept;
    [[nodiscard]] static EditorMenuRects ResolveMenu(
        const RECT& rect, EditorMenuCommand openMenu, int rowCount) noexcept;
    [[nodiscard]] static EditorToolbarRects ResolveToolbar(const RECT& rect) noexcept;
    [[nodiscard]] static EditorStatusBarRects ResolveStatusBar(const RECT& rect) noexcept;
    // Right edge of the last menu title in the top bar `rect`; the toolbar starts past it.
    [[nodiscard]] static int MenuTitlesRight(const RECT& rect) noexcept;
    [[nodiscard]] static EditorMenuCommand HitTestMenu(const EditorMenuRects& rects, int x, int y) noexcept;
    [[nodiscard]] static std::optional<int> HitTestMenuRow(const EditorMenuRects& rects, int x, int y) noexcept;
    [[nodiscard]] static EditorTransportCommand HitTestTransport(const EditorToolbarRects& rects, int x, int y) noexcept;
    [[nodiscard]] static bool HitTestSave(const EditorToolbarRects& rects, int x, int y) noexcept;
    [[nodiscard]] static EditorTopBarItem HitTestTopBarItem(const EditorToolbarRects& rects, int x, int y) noexcept;
    // False over the bar's empty space, which drags the window.
    [[nodiscard]] static bool IsInteractive(const EditorMenuRects& menu, const EditorToolbarRects& toolbar, int x, int y) noexcept;
    [[nodiscard]] static int MenuIndex(EditorMenuCommand menu) noexcept;
    [[nodiscard]] static RECT MenuRectByCommand(const EditorMenuRects& rects, EditorMenuCommand menu) noexcept;
    // How many rows a menu whose contents the build fixes shows. Zero for Layout,
    // whose rows are the layouts the project holds.
    [[nodiscard]] static int FixedRowCount(EditorMenuCommand menu) noexcept;
    // The menu's title and its rows in the editor language.
    [[nodiscard]] static std::string_view MenuLabel(EditorMenuCommand menu);
    [[nodiscard]] static std::string_view DropdownLabel(EditorMenuCommand menu, int row);
    // A fixed row's icon, its keyboard shortcut (empty when none is bound) and whether a
    // divider sets it apart from the row above.
    [[nodiscard]] static HeroIconKind DropdownIcon(EditorMenuCommand menu, int row) noexcept;
    [[nodiscard]] static std::string_view DropdownShortcut(EditorMenuCommand menu, int row) noexcept;
    // `row` counts every row of the menu here; `rowCount` is how many it shows now.
    [[nodiscard]] static bool DropdownStartsGroup(EditorMenuCommand menu, int row, int rowCount) noexcept;
    [[nodiscard]] static bool DropdownHasHeading(EditorMenuCommand menu, int row, int rowCount) noexcept;
    [[nodiscard]] static std::string_view DropdownHeading(EditorMenuCommand menu, int row, int rowCount);
    // The line under a fixed row's label; empty when the label says it all.
    [[nodiscard]] static std::string_view DropdownDescription(EditorMenuCommand menu, int row);
#endif
};

} // namespace kb::editor
