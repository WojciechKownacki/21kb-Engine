#include "rendering/HierarchyPanelStyle.hpp"

#if defined(_WIN32)

namespace kb::editor {
namespace {

[[nodiscard]] COLORREF Color(EditorColor color) noexcept {
    return RGB(color.r, color.g, color.b);
}

constexpr EditorTheme kTheme = MakeEditorDarkTheme();

} // namespace

COLORREF HierarchyPanelStyle::PanelBackground() noexcept {
    return Color(kTheme.panel);
}

COLORREF HierarchyPanelStyle::HeaderBackground() noexcept {
    return Color(kTheme.strip);
}

COLORREF HierarchyPanelStyle::HeaderBorder() noexcept {
    return Color(kTheme.borderChrome);
}

COLORREF HierarchyPanelStyle::SearchBackground() noexcept {
    return Color(kTheme.chrome);
}

COLORREF HierarchyPanelStyle::SearchBorder() noexcept {
    return Color(kTheme.borderPanel);
}

COLORREF HierarchyPanelStyle::SearchFocusedBorder() noexcept {
    return Color(kTheme.accent);
}

COLORREF HierarchyPanelStyle::SearchPlaceholderText() noexcept {
    return Color(kTheme.textDisabled);
}

COLORREF HierarchyPanelStyle::RowSelected() noexcept {
    return Color(kTheme.tabActive);
}

COLORREF HierarchyPanelStyle::RowHover() noexcept {
    return Color(kTheme.toolbarButton);
}

COLORREF HierarchyPanelStyle::RowText() noexcept {
    return Color(kTheme.textSecondary);
}

COLORREF HierarchyPanelStyle::RowTextSelected() noexcept {
    return Color(kTheme.textPrimary);
}

COLORREF HierarchyPanelStyle::RowTextHidden() noexcept {
    return Color(kTheme.textDisabled);
}

COLORREF HierarchyPanelStyle::CubeStroke() noexcept {
    return Color(kTheme.textSecondary);
}

COLORREF HierarchyPanelStyle::PrefabCubeStroke() noexcept {
    return Color(kTheme.accent);
}

COLORREF HierarchyPanelStyle::CubeFill() noexcept {
    return Color(kTheme.borderPanel);
}

COLORREF HierarchyPanelStyle::MutedText() noexcept {
    return Color(kTheme.textSecondary);
}

} // namespace kb::editor

#endif
