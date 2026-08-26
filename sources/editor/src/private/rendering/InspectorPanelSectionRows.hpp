#pragma once

#include "engine/scene/SceneTransforms.hpp"
#include "inspection/EditorValueFormatter.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/GdiResources.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/components/CategoryHeader.hpp"
#include "rendering/components/PropertyRow.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace kb::editor::inspector_panel_rows {

inline constexpr int kDisclosureRowHeight = 18;
inline constexpr int kDisclosureTextOffset = 29;
inline constexpr int kGroupRowHeight = 20;

namespace {

constexpr int kSectionHeaderHeight = 28;
constexpr int kFieldRowHeight = 26;
constexpr int kValueHeight = 22;
constexpr int kRowPadX = 14;
constexpr int kAssetPickerButtonSize = 18;
constexpr int kAssetPickerButtonGap = 3;
constexpr int kAxisLetterWidth = 11;
constexpr int kAxisGap = 6;
constexpr int kLaneGap = 5;
constexpr int kDividerHeight = 1;
constexpr int kCheckboxSize = 16;
constexpr int kTextBaselineOffsetY = 1;
[[nodiscard]] inline COLORREF Color(EditorColor color) {
    return GdiDrawing::ToColorRef(color);
}

[[nodiscard]] inline COLORREF Rgb(int red, int green, int blue) noexcept {
    return RGB(red, green, blue);
}

[[nodiscard]] inline COLORREF HoverFill(const EditorTheme& theme) noexcept {
    const COLORREF panel = Color(theme.panel);
    const COLORREF accent = Color(theme.accent);
    constexpr int accentPercent = 9;
    constexpr int panelPercent = 100 - accentPercent;
    return RGB(
        (GetRValue(panel) * panelPercent + GetRValue(accent) * accentPercent) / 100,
        (GetGValue(panel) * panelPercent + GetGValue(accent) * accentPercent) / 100,
        (GetBValue(panel) * panelPercent + GetBValue(accent) * accentPercent) / 100);
}

[[nodiscard]] inline RECT Rect(int left, int top, int right, int bottom) noexcept {
    return RECT{ left, top, right, bottom };
}

[[nodiscard]] inline RECT Shrink(RECT rect, int left, int top, int right, int bottom) noexcept {
    rect.left += left;
    rect.top += top;
    rect.right -= right;
    rect.bottom -= bottom;
    return rect;
}

[[nodiscard]] inline int CenteredY(const RECT& outer, int height) noexcept {
    return static_cast<int>(outer.top) + std::max(0, (static_cast<int>(outer.bottom - outer.top) - height) / 2);
}

[[nodiscard]] inline RECT CenteredRect(const RECT& outer, int left, int width, int height) noexcept {
    const int top = CenteredY(outer, height);
    return Rect(left, top, left + width, top + height);
}

[[nodiscard]] inline bool RowHovered(const InspectorPanelState& state, InspectorPropertyId property, int index = -1) noexcept {
    return property != InspectorPropertyId::None && state.HoveredProperty() == property && (index < 0 || state.HoveredIndex() == index);
}

inline void Text(HDC dc, RECT rect, std::string_view text, COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    if (text.empty()) return;
    rect.top += kTextBaselineOffsetY;
    rect.bottom += kTextBaselineOffsetY;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (wideLength <= 0) return;
    std::wstring wideText(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wideText.data(), wideLength) != wideLength) return;
    DrawTextW(dc, wideText.data(), wideLength, &rect, format | DT_NOPREFIX);
}

inline void TextW(HDC dc, RECT rect, std::wstring_view text, COLORREF color, UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
    rect.top += kTextBaselineOffsetY;
    rect.bottom += kTextBaselineOffsetY;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
}

inline void DrawFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
}

inline void DrawDivider(HDC dc, const EditorTheme& theme, int left, int right, int y) {
    GdiDrawing::FillRectColor(dc, Rect(left, y, right, y + kDividerHeight), Color(theme.borderChrome));
}

inline void DrawTriangle(HDC dc, RECT rect, bool expanded, COLORREF color) {
    ProjectFilesPanelDrawing::DrawDisclosureTriangle(dc, rect, color, expanded);
}

[[nodiscard]] inline RECT ComponentRemoveButtonRect(RECT header) noexcept {
    return CategoryHeader::Resolve(header, true, true, false).trailingAction;
}

[[nodiscard]] inline COLORREF AxisColor(char axis) noexcept {
    switch (axis) {
    case 'X': return Rgb(255, 66, 47);
    case 'Y': return Rgb(36, 123, 255);
    case 'Z': return Rgb(90, 216, 57);
    default: return Rgb(178, 184, 199);
    }
}

[[nodiscard]] inline std::string FormatFloat(float value) {
    return EditorValueFormatter::FormatFloat(value, 3);
}

} // namespace

[[nodiscard]] inline bool FieldValueHovered(
    const InspectorPanelState& state,
    InspectorSectionId section,
    InspectorPropertyId property,
    int index = -1) noexcept {
    // Property-less display fields must always carry an exact row index. Treating
    // -1 as the usual wildcard here highlights every read-only sibling.
    if (property == InspectorPropertyId::None && index < 0) {
        return false;
    }
    return state.IsHovered(InspectorHitKind::TextField, section, property, index) ||
        state.IsHovered(InspectorHitKind::FloatField, section, property, index);
}

inline void DrawSectionHeader(HDC dc, RECT rect, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, HeroIconKind icon, std::string_view title, bool removeButton = false) {
    const bool hovered = state.IsHovered(InspectorHitKind::SectionHeader, section, InspectorPropertyId::None);
    const bool actionHovered = removeButton &&
        state.IsHovered(InspectorHitKind::ComponentMenuButton, section, InspectorPropertyId::ComponentRemove);
    const CategoryHeaderLayout layout = CategoryHeader::Resolve(rect, true, removeButton, false);
    GdiDrawing::FillRectColor(dc, rect, hovered ? HoverFill(theme) : Color(theme.strip));
    if (hovered) {
        GdiDrawing::FillRectColor(dc, Rect(rect.left, rect.top, rect.left + 3, rect.bottom), Color(theme.accent));
    }
    DrawDivider(dc, theme, rect.left, rect.right, rect.bottom - 1);
    DrawTriangle(dc, Shrink(layout.disclosure, 4, 4, 4, 4), !state.IsCollapsed(section), Color(theme.textSecondary));
    HeroIconPainter::Draw(dc, layout.icon, icon, hovered ? Color(theme.accent) : Color(theme.textSecondary), 2);
    {
        ScopedFont font(12, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, font.handle);
        Text(dc, layout.title, title, Color(theme.textPrimary));
    }
    if (removeButton) {
        DrawFrame(
            dc,
            layout.trailingAction,
            actionHovered ? HoverFill(theme) : Color(theme.strip),
            actionHovered ? Color(theme.accent) : Color(theme.borderPanel));
        HeroIconPainter::Draw(
            dc,
            Shrink(layout.trailingAction, 4, 4, 4, 4),
            HeroIconKind::XMark,
            actionHovered ? Color(theme.textPrimary) : Color(theme.textDisabled),
            1);
    }
}

inline void DrawValueBox(HDC dc, RECT rect, const EditorTheme& theme, std::string_view value, bool hovered = false) {
    DrawFrame(
        dc,
        rect,
        hovered ? HoverFill(theme) : Color(theme.chrome),
        hovered ? Color(theme.accent) : Color(theme.borderPanel));
    ScopedFont font(12, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, font.handle);
    Text(dc, Shrink(rect, 9, 0, 4, 0), value, Color(theme.textPrimary));
}

inline void DrawAxisLane(HDC dc, RECT rect, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, char axis, std::string_view value) {
    const RECT letter = Rect(rect.left, rect.top, rect.left + kAxisLetterWidth, rect.bottom);
    {
        ScopedFont font(11, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, font.handle);
        char label[2]{ axis, '\0' };
        Text(dc, letter, label, AxisColor(axis), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    const bool editing = property != InspectorPropertyId::None && state.EditedProperty() == property;
    const std::string_view shown = editing ? std::string_view{ state.EditBuffer() } : value;
    const RECT box = Rect(letter.right + kAxisGap, CenteredY(rect, kValueHeight), rect.right, CenteredY(rect, kValueHeight) + kValueHeight);
    DrawValueBox(dc, box, theme, shown, state.IsHovered(InspectorHitKind::FloatField, section, property) || editing);
}

inline void DrawVec3Row(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, std::string_view label, const kb::scene::Vec3& value, InspectorPropertyId xProperty, InspectorPropertyId yProperty, InspectorPropertyId zProperty) {
    if (RowHovered(state, xProperty) || RowHovered(state, yProperty) || RowHovered(state, zProperty)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    const RECT valueRect = Rect(labelRect.right, row.top, row.right - kRowPadX, row.bottom);
    ScopedFont labelFont(12, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }
    const int valueWidth = static_cast<int>(valueRect.right - valueRect.left);
    const int laneWidth = std::max<int>(44, std::max(0, valueWidth - (kLaneGap * 2)) / 3);
    const int lanesWidth = laneWidth * 3 + kLaneGap * 2;
    const int laneLeft = static_cast<int>(valueRect.left) + std::max(0, (valueWidth - lanesWidth) / 2);
    const RECT x = Rect(laneLeft, valueRect.top, laneLeft + laneWidth, valueRect.bottom);
    const RECT y = Rect(x.right + kLaneGap, valueRect.top, x.right + kLaneGap + laneWidth, valueRect.bottom);
    const RECT z = Rect(y.right + kLaneGap, valueRect.top, y.right + kLaneGap + laneWidth, valueRect.bottom);
    DrawAxisLane(dc, x, theme, state, section, xProperty, 'X', FormatFloat(value.x));
    DrawAxisLane(dc, y, theme, state, section, yProperty, 'Y', FormatFloat(value.y));
    DrawAxisLane(dc, z, theme, state, section, zProperty, 'Z', FormatFloat(value.z));
}

inline void DrawRotationRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, std::string_view label, const kb::scene::Quat& value) {
    if (RowHovered(state, InspectorPropertyId::RotationX) || RowHovered(state, InspectorPropertyId::RotationY) || RowHovered(state, InspectorPropertyId::RotationZ)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    const RECT valueRect = Rect(labelRect.right, row.top, row.right - kRowPadX, row.bottom);
    ScopedFont labelFont(12, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }
    const int valueWidth = static_cast<int>(valueRect.right - valueRect.left);
    const int laneWidth = std::max<int>(44, std::max(0, valueWidth - (kLaneGap * 2)) / 3);
    const int lanesWidth = laneWidth * 3 + kLaneGap * 2;
    const int laneLeft = static_cast<int>(valueRect.left) + std::max(0, (valueWidth - lanesWidth) / 2);
    const RECT x = Rect(laneLeft, valueRect.top, laneLeft + laneWidth, valueRect.bottom);
    const RECT y = Rect(x.right + kLaneGap, valueRect.top, x.right + kLaneGap + laneWidth, valueRect.bottom);
    const RECT z = Rect(y.right + kLaneGap, valueRect.top, y.right + kLaneGap + laneWidth, valueRect.bottom);
    DrawAxisLane(dc, x, theme, state, InspectorSectionId::Transform, InspectorPropertyId::RotationX, 'X', FormatFloat(value.x));
    DrawAxisLane(dc, y, theme, state, InspectorSectionId::Transform, InspectorPropertyId::RotationY, 'Y', FormatFloat(value.y));
    DrawAxisLane(dc, z, theme, state, InspectorSectionId::Transform, InspectorPropertyId::RotationZ, 'Z', FormatFloat(value.z));
}

// `editIndex` disambiguates rows that share one property id but are addressed by
// index (physics fields, script variables). Pass -1 for singular rows. The inline
// editor buffer is shown only in the specific row being edited, never in every
// sibling row that happens to reuse the same property id.
inline void DrawFieldRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, std::string_view label, std::string_view value, int editIndex = -1) {
    const bool indexedPassiveField = property == InspectorPropertyId::None && editIndex >= 0;
    const bool passiveHovered =
        indexedPassiveField &&
        (state.IsHovered(InspectorHitKind::Row, section, property, editIndex) ||
            state.IsHovered(InspectorHitKind::TextField, section, property, editIndex));
    const bool editing = property != InspectorPropertyId::None && state.EditedProperty() == property && (editIndex < 0 || state.EditIndex() == editIndex);
    const std::string_view shown = editing ? std::string_view{ state.EditBuffer() } : value;
    const bool valueHovered = FieldValueHovered(state, section, property, editIndex);
    const bool rowHovered = RowHovered(state, property, editIndex) || passiveHovered;
    GdiDrawing::FillRectColor(dc, row, rowHovered ? HoverFill(theme) : Color(theme.panel));
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    const RECT valueRect = Rect(labelRect.right, CenteredY(row, kValueHeight), row.right - kRowPadX, CenteredY(row, kValueHeight) + kValueHeight);
    {
        ScopedFont labelFont(12, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }
    DrawValueBox(dc, valueRect, theme, shown, valueHovered || editing);
}

inline void DrawPairLane(
    HDC dc,
    RECT lane,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    InspectorSectionId section,
    InspectorPropertyId property,
    std::string_view laneLabel,
    std::string_view value) {
    const int labelWidth = laneLabel.size() == 1U ? kAxisLetterWidth : 43;
    const RECT labelRect = Rect(lane.left, lane.top, lane.left + labelWidth, lane.bottom);
    {
        ScopedFont font(11, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, font.handle);
        const COLORREF color = laneLabel.size() == 1U
            ? AxisColor(laneLabel.front())
            : Color(theme.textDisabled);
        Text(dc, labelRect, laneLabel, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    const bool editing = state.EditedProperty() == property;
    const std::string_view shown = editing ? std::string_view{state.EditBuffer()} : value;
    const RECT box = Rect(
        labelRect.right + kAxisGap,
        CenteredY(lane, kValueHeight),
        lane.right,
        CenteredY(lane, kValueHeight) + kValueHeight);
    DrawValueBox(dc, box, theme, shown,
        state.IsHovered(InspectorHitKind::FloatField, section, property) || editing);
}

inline void DrawPairRow(
    HDC dc,
    RECT row,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    InspectorSectionId section,
    std::string_view label,
    std::string_view firstLabel,
    std::string_view firstValue,
    InspectorPropertyId firstProperty,
    std::string_view secondLabel,
    std::string_view secondValue,
    InspectorPropertyId secondProperty) {
    if (RowHovered(state, firstProperty) || RowHovered(state, secondProperty)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    const RECT valueRect = Rect(labelRect.right, row.top, row.right - kRowPadX, row.bottom);
    {
        ScopedFont labelFont(12, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }
    const int valueWidth = static_cast<int>(valueRect.right - valueRect.left);
    const int laneWidth = std::max(66, (valueWidth - kLaneGap) / 2);
    const RECT first = Rect(valueRect.left, valueRect.top, valueRect.left + laneWidth, valueRect.bottom);
    const RECT second = Rect(first.right + kLaneGap, valueRect.top, valueRect.right, valueRect.bottom);
    DrawPairLane(dc, first, theme, state, section, firstProperty, firstLabel, firstValue);
    DrawPairLane(dc, second, theme, state, section, secondProperty, secondLabel, secondValue);
}

inline void DrawBoolPairRow(
    HDC dc,
    RECT row,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    InspectorSectionId section,
    std::string_view label,
    std::string_view firstLabel,
    bool firstValue,
    InspectorPropertyId firstProperty,
    std::string_view secondLabel,
    bool secondValue,
    InspectorPropertyId secondProperty) {
    if (RowHovered(state, firstProperty) || RowHovered(state, secondProperty)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    const RECT valueRect = Rect(labelRect.right, row.top, row.right - kRowPadX, row.bottom);
    {
        ScopedFont labelFont(12, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }
    const int laneWidth = std::max(66, (static_cast<int>(valueRect.right - valueRect.left) - kLaneGap) / 2);
    const std::array<RECT, 2U> lanes{
        Rect(valueRect.left, valueRect.top, valueRect.left + laneWidth, valueRect.bottom),
        Rect(valueRect.left + laneWidth + kLaneGap, valueRect.top, valueRect.right, valueRect.bottom),
    };
    const std::array<std::string_view, 2U> labels{firstLabel, secondLabel};
    const std::array<bool, 2U> values{firstValue, secondValue};
    const std::array<InspectorPropertyId, 2U> properties{firstProperty, secondProperty};
    for (std::size_t index = 0U; index < lanes.size(); ++index) {
        const RECT box = CenteredRect(lanes[index], lanes[index].right - kCheckboxSize, kCheckboxSize, kCheckboxSize);
        Text(dc, Rect(lanes[index].left, lanes[index].top, box.left - 5, lanes[index].bottom), labels[index], Color(theme.textDisabled), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        const bool hovered = state.IsHovered(InspectorHitKind::BoolField, section, properties[index]);
        DrawFrame(
            dc,
            box,
            values[index] ? Color(theme.accent) : (hovered ? HoverFill(theme) : Color(theme.chrome)),
            values[index] || hovered ? Color(theme.accent) : Color(theme.borderPanel));
        if (values[index]) {
            ScopedFont markFont(10, FW_SEMIBOLD);
            const ScopedGdiObject selectedFont(dc, markFont.handle);
            RECT glyph = box;
            glyph.top -= 1;
            glyph.bottom -= 1;
            TextW(dc, glyph, L"\u2714", Color(theme.textPrimary));
        }
    }
}

inline void DrawGroupRow(HDC dc, RECT row, const EditorTheme& theme, std::string_view label) {
    GdiDrawing::FillRectColor(dc, row, Color(theme.chrome));
    ScopedFont font(11, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, font.handle);
    Text(dc, Rect(row.left + kRowPadX, row.top, row.right - kRowPadX, row.bottom), label, Color(theme.textDisabled));
}

inline void DrawActionRow(
    HDC dc,
    RECT row,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    InspectorSectionId section,
    InspectorPropertyId property,
    std::string_view label,
    bool accent) {
    const bool hovered = state.IsHovered(InspectorHitKind::Row, section, property) ||
        state.IsHovered(InspectorHitKind::TextField, section, property);
    const RECT button = Shrink(row, kRowPadX, 2, kRowPadX, 2);
    DrawFrame(
        dc, button,
        hovered ? HoverFill(theme) : Color(theme.chrome),
        accent || hovered ? Color(theme.accent) : Color(theme.borderPanel));
    ScopedFont font(12, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, font.handle);
    Text(dc, button, label, accent ? Color(theme.textPrimary) : Color(theme.textSecondary),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

inline void DrawTagFieldRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state,
    InspectorSectionId section, InspectorPropertyId property, std::string_view label, std::string_view value) {
    if (RowHovered(state, property)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    const RECT valueRect = Rect(labelRect.right, CenteredY(row, kValueHeight), row.right - kRowPadX, CenteredY(row, kValueHeight) + kValueHeight);
    {
        ScopedFont labelFont(12, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }

    const bool hovered = FieldValueHovered(state, section, property);
    const bool classified = !value.empty();
    const bool open = state.IsTagsDropdownOpen();
    DrawFrame(dc, valueRect, hovered || open ? HoverFill(theme) : Color(theme.chrome),
        open ? Color(theme.textDisabled) : Color(theme.borderPanel));
    {
        ScopedFont valueFont(12, classified ? FW_SEMIBOLD : FW_NORMAL);
        const ScopedGdiObject selectedFont(dc, valueFont.handle);
        Text(dc, Shrink(valueRect, 8, 0, 28, 0), classified ? value : std::string_view{ "None" },
            classified ? Color(theme.textPrimary) : Color(theme.textDisabled));
        Text(dc, Rect(valueRect.right - 25, valueRect.top, valueRect.right - 5, valueRect.bottom), open ? "^" : "v",
            Color(theme.textSecondary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

[[nodiscard]] inline RECT AssetPickerButtonRect(RECT valueRect) noexcept {
    const int top = CenteredY(valueRect, kAssetPickerButtonSize);
    return Rect(valueRect.right - kAssetPickerButtonSize - 1, top, valueRect.right - 1, top + kAssetPickerButtonSize);
}

[[nodiscard]] inline RECT AssetPickerTextRect(RECT valueRect) noexcept {
    valueRect.right -= kAssetPickerButtonSize + kAssetPickerButtonGap;
    return valueRect;
}

inline void DrawAssetFieldRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, InspectorPropertyId buttonProperty, std::string_view label, std::string_view value) {
    if (RowHovered(state, property) || RowHovered(state, buttonProperty)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    const RECT valueRect = Rect(labelRect.right, CenteredY(row, kValueHeight), row.right - kRowPadX, CenteredY(row, kValueHeight) + kValueHeight);
    const RECT textRect = AssetPickerTextRect(valueRect);
    const bool valueHovered = state.IsHovered(InspectorHitKind::TextField, section, property);
    const bool buttonHovered = state.IsHovered(InspectorHitKind::TextField, section, buttonProperty);
    ScopedFont labelFont(12, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }
    DrawValueBox(dc, textRect, theme, value, valueHovered);
    const RECT button = AssetPickerButtonRect(valueRect);
    DrawFrame(dc, button, buttonHovered ? HoverFill(theme) : Color(theme.chrome), buttonHovered ? Color(theme.accent) : Color(theme.borderPanel));
    HeroIconPainter::Draw(dc, Shrink(button, 3, 3, 3, 3), HeroIconKind::MagnifyingGlass, buttonHovered ? Color(theme.textPrimary) : Color(theme.textSecondary), 1);
}

[[nodiscard]] inline RECT CheckboxRectForRow(RECT row) noexcept {
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    return CenteredRect(row, labelRect.right, kCheckboxSize, kCheckboxSize);
}

inline void DrawBoolRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, std::string_view label, bool checked, int editIndex = -1) {
    if (RowHovered(state, property, editIndex)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    const RECT box = CheckboxRectForRow(row);
    ScopedFont labelFont(12, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }
    const bool hovered = state.IsHovered(InspectorHitKind::BoolField, section, property, editIndex);
    DrawFrame(
        dc,
        box,
        checked ? Color(theme.accent) : (hovered ? HoverFill(theme) : Color(theme.chrome)),
        checked || hovered ? Color(theme.accent) : Color(theme.borderPanel));
    if (checked) {
        ScopedFont markFont(10, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, markFont.handle);
        RECT glyph = box;
        glyph.top -= 1;
        glyph.bottom -= 1;
        TextW(dc, glyph, L"\u2714", Color(theme.textPrimary));
    }
}

struct DisplayField {
    std::string_view label;
    std::string_view value;
};

inline void DrawDisclosureRow(
    HDC dc,
    RECT row,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    InspectorSectionId section,
    InspectorPropertyId property,
    std::string_view label,
    bool expanded) {
    const bool hovered = state.IsHovered(InspectorHitKind::Row, section, property);
    GdiDrawing::FillRectColor(dc, row, hovered ? HoverFill(theme) : Color(theme.panel));
    // Match the disclosure triangle geometry used by top-level category headers.
    const RECT chevron = Rect(row.left + 9, row.top, row.left + kDisclosureTextOffset, row.bottom);
    DrawTriangle(dc, Shrink(chevron, 4, 4, 4, 4), expanded, Color(theme.textSecondary));
    ScopedFont font(11, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, font.handle);
    Text(
        dc,
        Rect(row.left + kDisclosureTextOffset, row.top, row.right - kDisclosureTextOffset, row.bottom),
        label,
        Color(theme.textSecondary),
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

class SectionWriter {
public:
    SectionWriter(HDC dc, RECT bounds, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, HeroIconKind icon, std::string_view title, bool menuButton = false)
        : dc_(dc), bounds_(bounds), theme_(theme), state_(state), section_(section), collapsed_(state.IsCollapsed(section)), y_(bounds.top) {
        DrawSectionHeader(dc_, Rect(bounds_.left, y_, bounds_.right, y_ + kSectionHeaderHeight), theme_, state_, section_, icon, title, menuButton);
        y_ += kSectionHeaderHeight;
        if (!collapsed_) {
            DrawDivider(dc_, theme_, bounds_.left, bounds_.right, y_);
            y_ += kDividerHeight;
        }
    }

    void Field(std::string_view label, std::string_view value, InspectorPropertyId property = InspectorPropertyId::None, int editIndex = -1) { if (!collapsed_) { DrawFieldRow(dc_, Row(), theme_, state_, section_, property, label, value, editIndex); Advance(); } }
    void Tag(std::string_view label, std::string_view value, InspectorPropertyId property) { if (!collapsed_) { DrawTagFieldRow(dc_, Row(), theme_, state_, section_, property, label, value); Advance(); } }
    void AssetField(std::string_view label, std::string_view value, InspectorPropertyId property, InspectorPropertyId buttonProperty) { if (!collapsed_) { DrawAssetFieldRow(dc_, Row(), theme_, state_, section_, property, buttonProperty, label, value); Advance(); } }
    void Float(std::string_view label, std::string_view value, InspectorPropertyId property) { Field(label, value, property); }
    void Bool(std::string_view label, bool value, InspectorPropertyId property = InspectorPropertyId::None, int editIndex = -1) { if (!collapsed_) { DrawBoolRow(dc_, Row(), theme_, state_, section_, property, label, value, editIndex); Advance(); } }
    void Vec3(std::string_view label, const kb::scene::Vec3& value, InspectorPropertyId x, InspectorPropertyId y, InspectorPropertyId z) { if (!collapsed_) { DrawVec3Row(dc_, Row(), theme_, state_, section_, label, value, x, y, z); Advance(); } }
    void Pair(std::string_view label, std::string_view firstLabel, std::string_view firstValue, InspectorPropertyId firstProperty, std::string_view secondLabel, std::string_view secondValue, InspectorPropertyId secondProperty) { if (!collapsed_) { DrawPairRow(dc_, Row(), theme_, state_, section_, label, firstLabel, firstValue, firstProperty, secondLabel, secondValue, secondProperty); Advance(); } }
    void BoolPair(std::string_view label, std::string_view firstLabel, bool firstValue, InspectorPropertyId firstProperty, std::string_view secondLabel, bool secondValue, InspectorPropertyId secondProperty) { if (!collapsed_) { DrawBoolPairRow(dc_, Row(), theme_, state_, section_, label, firstLabel, firstValue, firstProperty, secondLabel, secondValue, secondProperty); Advance(); } }
    void Group(std::string_view label) { if (!collapsed_) { DrawGroupRow(dc_, Rect(bounds_.left, y_, bounds_.right, y_ + kGroupRowHeight), theme_, label); y_ += kGroupRowHeight; DrawDivider(dc_, theme_, bounds_.left, bounds_.right, y_); y_ += kDividerHeight; } }
    void Action(std::string_view label, InspectorPropertyId property, bool accent = false) { if (!collapsed_) { DrawActionRow(dc_, Row(), theme_, state_, section_, property, label, accent); Advance(); } }
    [[nodiscard]] RECT Reserve(int height) noexcept {
        if (collapsed_ || height <= 0) {
            return {};
        }
        const RECT reserved = Rect(bounds_.left, y_, bounds_.right, y_ + height);
        y_ += height;
        return reserved;
    }
    void Rotation(std::string_view label, const kb::scene::Quat& value) { if (!collapsed_) { DrawRotationRow(dc_, Row(), theme_, state_, label, value); Advance(); } }
    void Disclosure(std::string_view label, InspectorPropertyId property, bool expanded) {
        if (collapsed_) {
            return;
        }
        DrawDisclosureRow(dc_, Rect(bounds_.left, y_, bounds_.right, y_ + kDisclosureRowHeight), theme_, state_, section_, property, label, expanded);
        y_ += kDisclosureRowHeight;
        DrawDivider(dc_, theme_, bounds_.left, bounds_.right, y_);
        y_ += kDividerHeight;
    }
    void AnimatedFields(std::span<const DisplayField> fields, float expansion, int hoverIndexBase = 0) {
        if (collapsed_ || fields.empty()) {
            return;
        }
        const int fullHeight = static_cast<int>(fields.size()) * (kFieldRowHeight + kDividerHeight);
        const int visibleHeight = std::clamp(static_cast<int>(std::lround(static_cast<float>(fullHeight) * expansion)), 0, fullHeight);
        if (visibleHeight == 0) {
            return;
        }
        // Child rows align their label text with the disclosure title's leading
        // edge. DrawFieldRow adds kRowPadX internally, so inset its bounds by
        // only the remaining distance.
        const int contentLeft = bounds_.left + kDisclosureTextOffset - kRowPadX;
        const int savedDc = SaveDC(dc_);
        IntersectClipRect(dc_, contentLeft, y_, bounds_.right, y_ + visibleHeight);
        int drawY = y_;
        for (std::size_t index = 0; index < fields.size(); ++index) {
            const DisplayField& field = fields[index];
            DrawFieldRow(
                dc_,
                Rect(contentLeft, drawY, bounds_.right, drawY + kFieldRowHeight),
                theme_,
                state_,
                section_,
                InspectorPropertyId::None,
                field.label,
                field.value,
                hoverIndexBase + static_cast<int>(index));
            drawY += kFieldRowHeight;
            DrawDivider(dc_, theme_, contentLeft, bounds_.right, drawY);
            drawY += kDividerHeight;
        }
        RestoreDC(dc_, savedDc);
        y_ += visibleHeight;
    }
    [[nodiscard]] int Bottom() const noexcept { return y_; }

private:
    [[nodiscard]] RECT Row() const noexcept { return Rect(bounds_.left, y_, bounds_.right, y_ + kFieldRowHeight); }
    void Advance() { y_ += kFieldRowHeight; DrawDivider(dc_, theme_, bounds_.left, bounds_.right, y_); y_ += kDividerHeight; }
    HDC dc_ = nullptr;
    RECT bounds_{};
    const EditorTheme& theme_;
    const InspectorPanelState& state_;
    InspectorSectionId section_ = InspectorSectionId::None;
    bool collapsed_ = false;
    int y_ = 0;
};

} // namespace kb::editor::inspector_panel_rows
