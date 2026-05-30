#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct HierarchyToolbarLayoutRects {
    RECT header{};
    RECT bottomLine{};
    RECT addButton{};
    RECT searchBox{};
    RECT searchText{};
    RECT searchIcon{};
    RECT optionsButton{};
    RECT listContent{};
};

class HierarchyToolbarLayout {
public:
    HierarchyToolbarLayout() = delete;

    [[nodiscard]] static HierarchyToolbarLayoutRects Resolve(const RECT& content) noexcept;
    [[nodiscard]] static RECT IconRect(const RECT& button) noexcept;
};

#endif

} // namespace kb::editor
