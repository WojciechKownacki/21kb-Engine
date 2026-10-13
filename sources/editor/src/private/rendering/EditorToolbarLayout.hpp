#pragma once

#include "rendering/EditorToolbarRenderer.hpp"

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
#endif
};

} // namespace kb::editor
