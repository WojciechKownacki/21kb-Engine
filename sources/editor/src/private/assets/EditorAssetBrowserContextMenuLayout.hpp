#pragma once

#include "assets/EditorAssetBrowserLayout.hpp"

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserContextMenuLayout {
public:
    EditorAssetBrowserContextMenuLayout() = delete;

    [[nodiscard]] static RECT ContextMenuRect(const RECT& content, int x, int y, int itemCount) noexcept;
    [[nodiscard]] static RECT ContextMenuItemRect(const RECT& menu, int index) noexcept;
};

#endif

} // namespace kb::editor
