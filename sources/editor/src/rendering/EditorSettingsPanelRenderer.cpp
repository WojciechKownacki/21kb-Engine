#include "rendering/EditorSettingsPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/EditorSceneContext.hpp"

#include <array>

namespace kb::editor {
namespace {

constexpr int kPadding = 16;
constexpr int kHeaderHeight = 46;
constexpr int kSidebarWidth = 190;
constexpr int kCategoryTopInset = 8;
constexpr int kCategoryRowHeight = 30;
constexpr int kSectionHeaderHeight = 26;
constexpr int kRowHeight = 34;
constexpr int kRowGap = 6;
constexpr int kLabelWidth = 150;
constexpr int kCheckboxSize = 18;
constexpr int kIntervalButtonWidth = 72;
constexpr int kIntervalButtonGap = 6;

[[nodiscard]] COLORREF Color(EditorColor color) noexcept {
    return GdiDrawing::ToColorRef(color);
}

[[nodiscard]] COLORREF Blend(COLORREF a, COLORREF b, int percentB) noexcept {
    const int percentA = 100 - percentB;
    return RGB(
        (GetRValue(a) * percentA + GetRValue(b) * percentB) / 100,
        (GetGValue(a) * percentA + GetGValue(b) * percentB) / 100,
        (GetBValue(a) * percentA + GetBValue(b) * percentB) / 100);
}

struct EditorSettingsLayout {
    RECT header{};
    RECT sidebar{};
    RECT divider{};
    RECT content{};
    RECT sectionHeader{};
    RECT autosaveLabel{};
    RECT autosaveCheckbox{};
    RECT intervalLabel{};
    std::array<RECT, 5> intervalButtons{};
};

[[nodiscard]] EditorSettingsLayout Resolve(const RECT& content) noexcept {
    EditorSettingsLayout rects{};
    rects.header = {content.left, content.top, content.right, content.top + kHeaderHeight};
    rects.sidebar = {content.left, rects.header.bottom, content.left + kSidebarWidth, content.bottom};
    rects.divider = {rects.sidebar.right, rects.header.bottom, rects.sidebar.right + 1, content.bottom};
    rects.content = {rects.divider.right, rects.header.bottom, content.right, content.bottom};

    const int left = rects.content.left + kPadding;
    const int right = rects.content.right - kPadding;
    const int sectionTop = rects.content.top + kPadding;
    rects.sectionHeader = {left, sectionTop, right, sectionTop + kSectionHeaderHeight};

    const int autosaveTop = rects.sectionHeader.bottom + kRowGap;
    rects.autosaveLabel = {left, autosaveTop, left + kLabelWidth, autosaveTop + kRowHeight};
    const int checkboxTop = autosaveTop + (kRowHeight - kCheckboxSize) / 2;
    rects.autosaveCheckbox = {
        left + kLabelWidth,
        checkboxTop,
        left + kLabelWidth + kCheckboxSize,
        checkboxTop + kCheckboxSize,
    };

    const int intervalTop = rects.autosaveLabel.bottom + kRowGap;
    rects.intervalLabel = {left, intervalTop, left + kLabelWidth, intervalTop + kRowHeight};
    int optionLeft = left + kLabelWidth;
    for (RECT& button : rects.intervalButtons) {
        button = {
            optionLeft,
            intervalTop + 3,
            optionLeft + kIntervalButtonWidth,
            intervalTop + kRowHeight - 3,
        };
        optionLeft = button.right + kIntervalButtonGap;
    }
    return rects;
}

[[nodiscard]] RECT CategoryRow(const RECT& sidebar) noexcept {
    const int top = sidebar.top + kCategoryTopInset;
    return {sidebar.left + 6, top, sidebar.right - 6, top + kCategoryRowHeight};
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void DrawText(
    HDC dc,
    RECT rect,
    const char* text,
    COLORREF color,
    int pointSize = 12,
    int weight = FW_NORMAL,
    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    ScopedFont font{pointSize, weight};
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, static_cast<int>(flags | DT_NOPREFIX));
}

void DrawOptionButton(
    HDC dc,
    const RECT& rect,
    const EditorTheme& theme,
    const char* label,
    bool selected,
    bool enabled = true) {
    const COLORREF fill =
        selected ? Color(theme.accent) :
        Color(enabled ? theme.chrome : theme.background);
    const COLORREF border =
        selected ? Color(theme.accent) :
        Color(enabled ? theme.borderPanel : theme.borderChrome);
    const COLORREF text =
        Color(selected ? theme.textPrimary : (enabled ? theme.textSecondary : theme.textDisabled));
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    DrawText(
        dc,
        {rect.left + 8, rect.top, rect.right - 8, rect.bottom},
        label,
        text,
        12,
        selected ? FW_SEMIBOLD : FW_NORMAL,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawCheckbox(HDC dc, const RECT& rect, const EditorTheme& theme, bool checked) {
    const COLORREF fill = Color(checked ? theme.accent : theme.chrome);
    const COLORREF border = Color(checked ? theme.accent : theme.borderPanel);
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    if (checked) {
        DrawText(
            dc,
            rect,
            "x",
            Color(theme.textPrimary),
            12,
            FW_BOLD,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

[[nodiscard]] int AutosaveIntervalIndex(std::uint32_t minutes) noexcept {
    constexpr std::array<std::uint32_t, 5> intervals{{5U, 10U, 15U, 30U, 60U}};
    for (int index = 0; index < static_cast<int>(intervals.size()); ++index) {
        if (intervals[static_cast<std::size_t>(index)] == minutes) return index;
    }
    return -1;
}

} // namespace

void EditorSettingsPanelRenderer::Paint(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext) const {
    if (content.right <= content.left || content.bottom <= content.top) return;

    const EditorSettingsLayout rects = Resolve(content);
    const EditorSavingPreferences preferences = sceneContext.CaptureEditorSavingPreferences();

    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));

    GdiDrawing::FillRectColor(dc, rects.header, Color(theme.strip));
    GdiDrawing::FillRectColor(dc, {rects.header.left, rects.header.top, rects.header.left + 3, rects.header.bottom}, Color(theme.accent));
    GdiDrawing::FillRectColor(
        dc,
        {rects.header.left, rects.header.bottom - 1, rects.header.right, rects.header.bottom},
        Color(theme.borderChrome));
    DrawText(
        dc,
        {rects.header.left + kPadding, rects.header.top, rects.header.right - kPadding, rects.header.bottom},
        "Editor Settings",
        Color(theme.textPrimary),
        14,
        FW_SEMIBOLD);

    GdiDrawing::FillRectColor(dc, rects.sidebar, Color(theme.chrome));
    GdiDrawing::FillRectColor(dc, rects.divider, Color(theme.borderChrome));
    const RECT category = CategoryRow(rects.sidebar);
    GdiDrawing::FillRectColor(dc, category, Blend(Color(theme.panel), Color(theme.accent), 18));
    GdiDrawing::FillRectColor(
        dc,
        {category.left, category.top, category.left + 2, category.bottom},
        Color(theme.accent));
    DrawText(
        dc,
        {category.left + 12, category.top, category.right - 8, category.bottom},
        "Saving",
        Color(theme.textPrimary),
        12,
        FW_SEMIBOLD);

    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(
        dc,
        {
            rects.sectionHeader.left + 8,
            rects.sectionHeader.top,
            rects.sectionHeader.right - 8,
            rects.sectionHeader.bottom,
        },
        "SAVING",
        Color(theme.textSecondary),
        11,
        FW_SEMIBOLD);

    DrawText(
        dc,
        {
            rects.autosaveLabel.left,
            rects.autosaveLabel.top,
            rects.autosaveLabel.right - 8,
            rects.autosaveLabel.bottom,
        },
        "Autosave",
        Color(theme.textSecondary),
        12);
    DrawCheckbox(dc, rects.autosaveCheckbox, theme, preferences.autosaveEnabled);

    DrawText(
        dc,
        {
            rects.intervalLabel.left,
            rects.intervalLabel.top,
            rects.intervalLabel.right - 8,
            rects.intervalLabel.bottom,
        },
        "Interval",
        Color(preferences.autosaveEnabled ? theme.textSecondary : theme.textDisabled),
        12);

    constexpr std::array<const char*, 5> labels{{"5 min", "10 min", "15 min", "30 min", "60 min"}};
    const int selectedInterval = AutosaveIntervalIndex(preferences.autosaveIntervalMinutes);
    for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
        DrawOptionButton(
            dc,
            rects.intervalButtons[static_cast<std::size_t>(index)],
            theme,
            labels[static_cast<std::size_t>(index)],
            preferences.autosaveEnabled && selectedInterval == index,
            preferences.autosaveEnabled);
    }
}

EditorSettingsHit EditorSettingsPanelRenderer::HitTest(
    const RECT& content,
    int x,
    int y) noexcept {
    const EditorSettingsLayout rects = Resolve(content);
    if (Contains(rects.autosaveLabel, x, y) ||
        Contains(rects.autosaveCheckbox, x, y)) {
        return {EditorSettingsHitKind::Toggle, 0, -1};
    }
    for (int index = 0; index < static_cast<int>(rects.intervalButtons.size()); ++index) {
        if (Contains(rects.intervalButtons[static_cast<std::size_t>(index)], x, y)) {
            return {EditorSettingsHitKind::Choice, 1, index};
        }
    }
    return {};
}

} // namespace kb::editor
#endif
