#include "rendering/HierarchyPanelStyle.hpp"

#if defined(_WIN32)

namespace kb::editor {

COLORREF HierarchyPanelStyle::PanelBackground() noexcept {
    return RGB(25, 26, 28);
}

COLORREF HierarchyPanelStyle::HeaderBackground() noexcept {
    return RGB(22, 23, 25);
}

COLORREF HierarchyPanelStyle::HeaderBorder() noexcept {
    return RGB(58, 61, 66);
}

COLORREF HierarchyPanelStyle::SearchBackground() noexcept {
    return RGB(30, 31, 34);
}

COLORREF HierarchyPanelStyle::SearchBorder() noexcept {
    return RGB(72, 75, 81);
}

COLORREF HierarchyPanelStyle::SearchFocusedBorder() noexcept {
    return RGB(140, 158, 190);
}

COLORREF HierarchyPanelStyle::SearchPlaceholderText() noexcept {
    return RGB(124, 128, 136);
}

COLORREF HierarchyPanelStyle::RowSelected() noexcept {
    return RGB(60, 63, 68);
}

COLORREF HierarchyPanelStyle::RowHover() noexcept {
    return RGB(45, 47, 51);
}

COLORREF HierarchyPanelStyle::RowText() noexcept {
    return RGB(211, 216, 224);
}

COLORREF HierarchyPanelStyle::RowTextSelected() noexcept {
    return RGB(245, 245, 245);
}

COLORREF HierarchyPanelStyle::RowTextHidden() noexcept {
    return RGB(116, 121, 130);
}

COLORREF HierarchyPanelStyle::CubeStroke() noexcept {
    return RGB(174, 184, 194);
}

COLORREF HierarchyPanelStyle::PrefabCubeStroke() noexcept {
    return RGB(68, 145, 236);
}

COLORREF HierarchyPanelStyle::CubeFill() noexcept {
    return RGB(76, 85, 95);
}

COLORREF HierarchyPanelStyle::MutedText() noexcept {
    return RGB(148, 154, 164);
}

} // namespace kb::editor

#endif
