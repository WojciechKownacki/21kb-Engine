#include "rendering/EditorSettingsPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/components/CategoryHeader.hpp"
#include "rendering/components/DenseListRow.hpp"
#include "rendering/components/PropertyRow.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace kb::editor {
namespace {

constexpr int kHeaderHeight = 52;
constexpr int kSidebarWidth = 220;
constexpr int kSidebarHeadingHeight = 28;
constexpr int kNavigationRowHeight = 32;
constexpr int kSectionHeight = 28;
constexpr int kPropertyHeight = 34;
constexpr int kPadding = 14;

struct CategoryInfo {
    std::string_view title;
    std::string_view description;
    HeroIconKind icon;
};

constexpr std::array<CategoryInfo, 4> kCategories{{
    {"Viewport Rendering", "Quality and backend used by editor preview surfaces.", HeroIconKind::Camera},
    {"Viewport Controls", "World grid and transform snapping defaults.", HeroIconKind::RotationSnap},
    {"Saving", "Automatic protection of open documents.", HeroIconKind::DocumentText},
    {"Project Files", "Personal defaults for browsing project content.", HeroIconKind::Folder},
}};

[[nodiscard]] COLORREF Color(EditorColor value) noexcept { return GdiDrawing::ToColorRef(value); }

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] RECT HeaderRect(const RECT& content) noexcept {
    return {content.left, content.top, content.right, std::min<LONG>(content.bottom, content.top + kHeaderHeight)};
}

[[nodiscard]] RECT SidebarRect(const RECT& content) noexcept {
    return {content.left, HeaderRect(content).bottom, std::min<LONG>(content.right, content.left + kSidebarWidth), content.bottom};
}

[[nodiscard]] RECT NavigationHeaderRect(const RECT& content) noexcept {
    const RECT sidebar = SidebarRect(content);
    return {sidebar.left, sidebar.top, sidebar.right, std::min<LONG>(sidebar.bottom, sidebar.top + kSidebarHeadingHeight)};
}

[[nodiscard]] RECT CategoryRect(const RECT& content, int index) noexcept {
    const RECT sidebar = SidebarRect(content);
    const int top = sidebar.top + kSidebarHeadingHeight + 6 + index * kNavigationRowHeight;
    return {sidebar.left + 6, top, sidebar.right - 6, std::min<LONG>(sidebar.bottom, top + kNavigationRowHeight)};
}

[[nodiscard]] RECT PageRect(const RECT& content) noexcept {
    const RECT sidebar = SidebarRect(content);
    return {std::min<LONG>(content.right, sidebar.right + 1), sidebar.top, content.right, content.bottom};
}

[[nodiscard]] RECT SectionRect(const RECT& content) noexcept {
    const RECT page = PageRect(content);
    return {page.left + kPadding, page.top + kPadding, page.right - kPadding, page.top + kPadding + kSectionHeight};
}

[[nodiscard]] RECT PropertyRect(const RECT& content, int row) noexcept {
    const RECT section = SectionRect(content);
    const int top = section.bottom + 6 + row * kPropertyHeight;
    return {section.left, top, section.right, top + kPropertyHeight};
}

[[nodiscard]] RECT ChoiceRect(const RECT& content, int row, int option, int count) noexcept {
    const PropertyRowLayout layout = PropertyRow::Resolve(PropertyRect(content, row));
    const int gap = 4;
    const int width = std::max(1, static_cast<int>(layout.value.right - layout.value.left) - gap * (count - 1));
    const int base = width / count;
    const int remainder = width % count;
    int left = layout.value.left;
    for (int index = 0; index < option; ++index) left += base + (index < remainder ? 1 : 0) + gap;
    const int optionWidth = base + (option < remainder ? 1 : 0);
    return {left, layout.value.top, left + optionWidth, layout.value.bottom};
}

void Text(HDC dc, RECT rect, std::string_view text, COLORREF color, int size, int weight, UINT format) {
    ScopedFont font{size, weight};
    const ScopedGdiObject selected(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
}

void PaintHeader(HDC dc, const RECT& content, const EditorTheme& theme) {
    const RECT header = HeaderRect(content);
    GdiDrawing::FillRectColor(dc, header, Color(theme.chrome));
    GdiDrawing::FillRectColor(dc, {header.left, header.bottom - 1, header.right, header.bottom}, Color(theme.borderChrome));
    Text(dc, {header.left + 16, header.top + 5, header.right - 12, header.top + 27}, "Editor Settings", Color(theme.textPrimary), 14, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(dc, {header.left + 16, header.top + 26, header.right - 12, header.bottom - 3}, "Private to this project on this workstation", Color(theme.textSecondary), 10, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintSidebar(HDC dc, const RECT& content, const EditorTheme& theme, int selected) {
    const RECT sidebar = SidebarRect(content);
    GdiDrawing::FillRectColor(dc, sidebar, Color(theme.panel));
    GdiDrawing::FillRectColor(dc, {sidebar.right, sidebar.top, sidebar.right + 1, sidebar.bottom}, Color(theme.borderChrome));
    CategoryHeader::Paint(dc, theme, {.bounds = NavigationHeaderRect(content), .title = "EDITOR", .icon = HeroIconKind::AdjustmentsHorizontal, .expanded = true});
    for (int index = 0; index < static_cast<int>(kCategories.size()); ++index) {
        DenseListRow::Paint(dc, theme, {
            .bounds = CategoryRect(content, index),
            .title = kCategories[static_cast<std::size_t>(index)].title,
            .icon = kCategories[static_cast<std::size_t>(index)].icon,
            .selected = selected == index,
            .showIcon = true,
        });
    }
}

void PaintChoice(HDC dc, const RECT& content, const EditorTheme& theme, int row, std::string_view label,
    const std::string_view* labels, int count, int selected, bool enabled = true) {
    const RECT bounds = PropertyRect(content, row);
    const PropertyRowLayout layout = PropertyRow::Resolve(bounds);
    PropertyRow::PaintBackground(dc, theme, bounds, false);
    PropertyRow::PaintLabel(dc, theme, layout.label, label, enabled);
    for (int index = 0; index < count; ++index) {
        const RECT option = ChoiceRect(content, row, index, count);
        GdiDrawing::DrawSharpFrame(dc, option,
            Color(index == selected ? theme.toolbarButton : theme.chrome),
            Color(index == selected ? theme.accent : theme.borderPanel));
        Text(dc, option, labels[index], Color(enabled ? theme.textPrimary : theme.textDisabled), 11,
            index == selected ? FW_SEMIBOLD : FW_NORMAL,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void PaintToggle(HDC dc, const RECT& content, const EditorTheme& theme, int row, std::string_view label, bool enabled) {
    PropertyRow::Paint(dc, theme, {
        .bounds = PropertyRect(content, row),
        .label = label,
        .value = enabled ? "Enabled" : "Disabled",
        .valueAlignment = PropertyRowValueAlignment::Center,
    });
}

void PaintSection(HDC dc, const RECT& content, const EditorTheme& theme, int category) {
    const CategoryInfo& info = kCategories[static_cast<std::size_t>(category)];
    CategoryHeader::Paint(dc, theme, {.bounds = SectionRect(content), .title = info.title, .trailingText = info.description, .icon = info.icon, .expanded = true});
}

void PaintRendering(HDC dc, const RECT& content, const EditorTheme& theme, const EditorRenderBackendSettings& p) {
    constexpr std::array<std::string_view, 3> backend{{"Auto", "DX12", "Vulkan"}};
    constexpr std::array<std::string_view, 4> aa{{"None", "FXAA", "TAA", "MSAA"}};
    constexpr std::array<std::string_view, 5> samples{{"Off", "2x", "4x", "8x", "16x"}};
    const std::uint8_t sampleCount = p.MsaaSamples();
    const int sampleIndex = sampleCount == 2U ? 1 : sampleCount == 4U ? 2 : sampleCount == 8U ? 3 : sampleCount == 16U ? 4 : 0;
    PaintChoice(dc, content, theme, 0, "Render Backend", backend.data(), 3, static_cast<int>(p.Backend()));
    PaintToggle(dc, content, theme, 1, "Post Process", p.PostProcessEnabled());
    PaintChoice(dc, content, theme, 2, "Anti-Aliasing", aa.data(), 4, static_cast<int>(p.AntiAliasingMode()));
    PaintChoice(dc, content, theme, 3, "MSAA Samples", samples.data(), 5, sampleIndex, p.AntiAliasingMode() == EditorAntiAliasingMode::Msaa);
    PaintToggle(dc, content, theme, 4, "Bloom", p.BloomEnabled());
    PaintToggle(dc, content, theme, 5, "Shadows", p.ShadowsEnabled());
    PaintToggle(dc, content, theme, 6, "Selection Outline", p.SelectionOutlineEnabled());
    PaintToggle(dc, content, theme, 7, "GPU Driven Submission", p.GpuDrivenEnabled());
}

[[nodiscard]] int FloatIndex(float value, const float* values, int count) noexcept {
    for (int index = 0; index < count; ++index) if (std::abs(value - values[index]) < 0.0001F) return index;
    return 0;
}

void PaintViewport(HDC dc, const RECT& content, const EditorTheme& theme, const EditorWorkspacePreferences& p) {
    constexpr std::array<std::string_view, 5> spacingLabels{{"0.1", "0.5", "1", "5", "10"}};
    constexpr std::array<float, 5> spacingValues{{0.1F, 0.5F, 1.0F, 5.0F, 10.0F}};
    constexpr std::array<std::string_view, 5> rotationLabels{{"Off", "5", "15", "30", "45"}};
    constexpr std::array<float, 5> rotationValues{{0.0F, 5.0F, 15.0F, 30.0F, 45.0F}};
    PaintToggle(dc, content, theme, 0, "World Grid", p.gridVisible);
    PaintChoice(dc, content, theme, 1, "Grid Spacing", spacingLabels.data(), 5, FloatIndex(p.gridSpacing, spacingValues.data(), 5));
    PaintToggle(dc, content, theme, 2, "Transform Snapping", p.snapEnabled);
    PaintChoice(dc, content, theme, 3, "Translation Step", spacingLabels.data(), 5, FloatIndex(p.snapStep, spacingValues.data(), 5));
    PaintChoice(dc, content, theme, 4, "Rotation Step", rotationLabels.data(), 5, FloatIndex(p.rotationSnapDegrees, rotationValues.data(), 5));
}

void PaintSaving(HDC dc, const RECT& content, const EditorTheme& theme, const EditorWorkspacePreferences& p) {
    constexpr std::array<std::string_view, 5> intervals{{"5 min", "10 min", "15 min", "30 min", "60 min"}};
    constexpr std::array<std::uint32_t, 5> values{{5U, 10U, 15U, 30U, 60U}};
    int selected = 0;
    for (int index = 0; index < 5; ++index) if (p.autosaveIntervalMinutes == values[static_cast<std::size_t>(index)]) selected = index;
    PaintToggle(dc, content, theme, 0, "Autosave", p.autosaveEnabled);
    PaintChoice(dc, content, theme, 1, "Interval", intervals.data(), 5, selected, p.autosaveEnabled);
}

void PaintProjectFiles(HDC dc, const RECT& content, const EditorTheme& theme, const EditorWorkspacePreferences& p) {
    constexpr std::array<std::string_view, 2> views{{"List", "Tiles"}};
    constexpr std::array<std::string_view, 3> sorts{{"Name", "Type", "Path"}};
    constexpr std::array<std::string_view, 4> scales{{"65%", "100%", "135%", "175%"}};
    constexpr std::array<float, 4> scaleValues{{0.65F, 1.0F, 1.35F, 1.75F}};
    PaintToggle(dc, content, theme, 0, "Recursive Search", p.assetBrowserRecursive);
    PaintChoice(dc, content, theme, 1, "Default View", views.data(), 2, static_cast<int>(p.assetViewMode));
    PaintChoice(dc, content, theme, 2, "Default Sort", sorts.data(), 3, static_cast<int>(p.assetSortMode));
    PaintToggle(dc, content, theme, 3, "Show Folders", p.assetShowFolders);
    PaintToggle(dc, content, theme, 4, "Show Templates", p.assetShowTemplates);
    PaintChoice(dc, content, theme, 5, "Thumbnail Scale", scales.data(), 4, FloatIndex(p.assetThumbnailScale, scaleValues.data(), 4));
}

[[nodiscard]] bool ToggleRow(int category, int row) noexcept {
    if (category == 0) return row == 1 || (row >= 4 && row <= 7);
    if (category == 1) return row == 0 || row == 2;
    if (category == 2) return row == 0;
    return category == 3 && (row == 0 || row == 3 || row == 4);
}

[[nodiscard]] int ChoiceCount(int category, int row) noexcept {
    if (category == 0) {
        if (row == 0) return 3;
        if (row == 2) return 4;
        if (row == 3) return 5;
    } else if (category == 1 && (row == 1 || row == 3 || row == 4)) return 5;
    else if (category == 2 && row == 1) return 5;
    else if (category == 3) {
        if (row == 1) return 2;
        if (row == 2) return 3;
        if (row == 5) return 4;
    }
    return 0;
}

[[nodiscard]] int RowCount(int category) noexcept {
    constexpr std::array<int, 4> counts{{8, 5, 2, 6}};
    return counts[static_cast<std::size_t>(std::clamp(category, 0, 3))];
}

} // namespace

void EditorSettingsPanelRenderer::Paint(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderer) const {
    if (content.right <= content.left || content.bottom <= content.top) return;
    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));
    const int category = std::clamp(sceneContext.EditorSettings().SelectedCategory(), 0, 3);
    PaintHeader(dc, content, theme);
    PaintSidebar(dc, content, theme, category);
    PaintSection(dc, content, theme, category);
    const EditorWorkspacePreferences preferences = sceneContext.CaptureEditorWorkspacePreferences();
    if (category == 0) PaintRendering(dc, content, theme, renderer);
    else if (category == 1) PaintViewport(dc, content, theme, preferences);
    else if (category == 2) PaintSaving(dc, content, theme, preferences);
    else PaintProjectFiles(dc, content, theme, preferences);
}

EditorSettingsHit EditorSettingsPanelRenderer::HitTest(
    const RECT& content,
    const EditorSceneContext& sceneContext,
    int x,
    int y) noexcept {
    for (int index = 0; index < static_cast<int>(kCategories.size()); ++index) {
        if (Contains(CategoryRect(content, index), x, y)) return {EditorSettingsHitKind::Category, index, -1};
    }
    const int category = std::clamp(sceneContext.EditorSettings().SelectedCategory(), 0, 3);
    for (int row = 0; row < RowCount(category); ++row) {
        if (ToggleRow(category, row) && Contains(PropertyRect(content, row), x, y)) {
            return {EditorSettingsHitKind::Toggle, row, -1};
        }
        const int choices = ChoiceCount(category, row);
        for (int option = 0; option < choices; ++option) {
            if (Contains(ChoiceRect(content, row, option, choices), x, y)) {
                return {EditorSettingsHitKind::Choice, row, option};
            }
        }
    }
    return {};
}

} // namespace kb::editor
#endif
