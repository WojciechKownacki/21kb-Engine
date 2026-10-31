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
    [[nodiscard]] static const std::array<EditorMenuDescriptor, 6>& MenuDescriptors() noexcept;
    [[nodiscard]] static EditorMenuRects ResolveMenu(
        const RECT& rect, EditorMenuCommand openMenu, int rowCount) noexcept;
    [[nodiscard]] static EditorToolbarRects ResolveToolbar(const RECT& rect) noexcept;
    // Right edge of the last menu title in the top bar `rect`; the toolbar starts past it.
    [[nodiscard]] static int MenuTitlesRight(const RECT& rect) noexcept;
    [[nodiscard]] static EditorMenuCommand HitTestMenu(const EditorMenuRects& rects, int x, int y) noexcept;
    [[nodiscard]] static std::optional<int> HitTestMenuRow(const EditorMenuRects& rects, int x, int y) noexcept;
    [[nodiscard]] static EditorTransportCommand HitTestTransport(const EditorToolbarRects& rects, int x, int y) noexcept;
    [[nodiscard]] static bool HitTestSave(const EditorToolbarRects& rects, int x, int y) noexcept;
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
    [[nodiscard]] static bool DropdownStartsGroup(EditorMenuCommand menu, int row) noexcept;
#endif
};

} // namespace kb::editor
