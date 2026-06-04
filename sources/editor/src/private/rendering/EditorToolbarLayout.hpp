#pragma once

#include "rendering/EditorToolbarRenderer.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace kb::editor {

struct EditorMenuDescriptor {
    EditorMenuCommand command = EditorMenuCommand::None;
    std::string_view label{};
    int width = 0;
};

class EditorToolbarLayout {
public:
#if defined(_WIN32)
    [[nodiscard]] static const std::array<EditorMenuDescriptor, 4>& MenuDescriptors() noexcept;
    [[nodiscard]] static EditorMenuRects ResolveMenu(const RECT& rect, EditorMenuCommand openMenu) noexcept;
    [[nodiscard]] static EditorToolbarRects ResolveToolbar(const RECT& rect) noexcept;
    [[nodiscard]] static EditorMenuCommand HitTestMenu(const EditorMenuRects& rects, int x, int y) noexcept;
    [[nodiscard]] static std::optional<int> HitTestMenuRow(const EditorMenuRects& rects, int x, int y) noexcept;
    [[nodiscard]] static EditorTransportCommand HitTestTransport(const EditorToolbarRects& rects, int x, int y) noexcept;
    [[nodiscard]] static int MenuIndex(EditorMenuCommand menu) noexcept;
    [[nodiscard]] static RECT MenuRectByCommand(const EditorMenuRects& rects, EditorMenuCommand menu) noexcept;
    [[nodiscard]] static std::string_view DropdownLabel(EditorMenuCommand menu, int row) noexcept;
#endif
};

} // namespace kb::editor
