#pragma once

#include "engine/scene/SceneTransforms.hpp"
#include "localization/EditorLocalization.hpp"
#include "inspection/EditorValueFormatter.hpp"
#include "inspection/InspectorComponentDefaults.hpp"
#include "inspection/InspectorFieldGuide.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "inspection/InspectorSceneColor.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/GdiResources.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/EditorPanelStyle.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/components/CategoryHeader.hpp"
#include "rendering/components/PropertyRow.hpp"
#include "rendering/gdi/EditorTextMeasure.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace kb::editor::inspector_panel_rows {

inline constexpr int kDisclosureRowHeight = 18;
inline constexpr int kDisclosureTextOffset = 29;
inline constexpr int kGroupRowHeight = 20;
inline constexpr int kValueRightInset = 3;
// The reset arrow a changed value shows at the right end of its label, and the entries of the component menu.
inline constexpr int kResetMarkerWidth = 18;

namespace {
// The shared panel look now lives in EditorPanelStyle; these names stay unqualified
// so every row builder below reads the same as it always did.
using panel_style::kSectionHeaderHeight;
using panel_style::kFieldRowHeight;
using panel_style::kValueHeight;
using panel_style::kRowPadX;
using panel_style::InspectorLabelColumnRight;
using panel_style::kAssetPickerButtonSize;
using panel_style::kAssetPickerButtonGap;
using panel_style::kAxisLetterWidth;
using panel_style::kAxisGap;
using panel_style::kLaneGap;
using panel_style::kDividerHeight;
using panel_style::kCheckboxSize;
using panel_style::kTextBaselineOffsetY;
using panel_style::kSectionDividerInset;
using panel_style::kSectionCornerRadius;
using panel_style::kSectionOutlineWidth;
using panel_style::kInputCornerRadius;
using panel_style::kInputOutlineWidth;
using panel_style::Color;
using panel_style::Rgb;
using panel_style::HoverFill;
using panel_style::Rect;
using panel_style::Shrink;
using panel_style::CenteredY;
using panel_style::CenteredRect;
using panel_style::Text;
using panel_style::TextW;
using panel_style::GdiplusColor;
using panel_style::AddRoundedRectPath;
using panel_style::AddTopRoundedRectPath;
using panel_style::ConfigureSectionGraphics;
using panel_style::DrawRoundedFrame;
using panel_style::DrawInputFrame;
using panel_style::DrawTextCaret;
using panel_style::DrawSectionButtonFrame;
using panel_style::DrawSectionAccentFrame;
using panel_style::FillSectionHeaderBackground;
using panel_style::DrawSectionCardOutline;
using panel_style::DrawDivider;
using panel_style::DrawTriangle;
[[nodiscard]] inline bool RowHovered(const InspectorPanelState& state, InspectorPropertyId property, int index = -1) noexcept {
    return property != InspectorPropertyId::None && state.HoveredProperty() == property && (index < 0 || state.HoveredIndex() == index);
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
        state.IsHovered(InspectorHitKind::ComponentMenuButton, section,
            InspectorComponentDefaults::Supports(section) ? InspectorPropertyId::ComponentMenu : InspectorPropertyId::ComponentRemove);
    const CategoryHeaderLayout layout = CategoryHeader::Resolve(rect, true, removeButton, false);
    FillSectionHeaderBackground(dc, rect, hovered ? HoverFill(theme) : Color(theme.header));
    DrawDivider(dc, theme, rect.left, rect.right, rect.bottom - 1);
    DrawTriangle(dc, Shrink(layout.disclosure, 4, 4, 4, 4), !state.IsCollapsed(section), Color(theme.textDisabled));
    HeroIconPainter::Draw(dc, layout.icon, icon, hovered ? Color(theme.textSecondary) : Color(theme.textDisabled), 2);
    {
        ScopedFont font(12, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, font.handle);
        Text(dc, layout.title, title, Color(theme.textPrimary));
    }
    if (removeButton) {
        // The button shows its frame only under the pointer; at rest it is just its glyph.
        if (actionHovered) {
            DrawInputFrame(dc, layout.trailingAction, HoverFill(theme), Color(theme.accent));
        }
        const bool menu = InspectorComponentDefaults::Supports(section);
        HeroIconPainter::Draw(
            dc,
            Shrink(layout.trailingAction, menu ? 3 : 4, menu ? 3 : 4, menu ? 3 : 4, menu ? 3 : 4),
            menu ? HeroIconKind::EllipsisHorizontal : HeroIconKind::XMark,
            actionHovered ? Color(theme.textPrimary) : (menu ? Color(theme.textSecondary) : Color(theme.textDisabled)),
            menu ? 2 : 1);
    }
}

// `hovered` and `editing` are deliberately separate. They used to be one flag, so a field being
// typed into looked exactly like one the pointer happened to be over, and there was no way to
// tell which field would receive the next keystroke.
//
// `unit` is written after the number in the box's right end. A `fill` from 0 to 1 draws the value as a slider: the
// box shows how far along its range the number is (clicking and dragging it already scrubs the value).
inline void DrawValueBox(HDC dc, RECT rect, const EditorTheme& theme, std::string_view value, bool hovered = false,
                         bool editing = false, bool caretVisible = false, std::string_view unit = {}, float fill = -1.0F) {
    DrawInputFrame(
        dc,
        rect,
        hovered || editing ? HoverFill(theme) : Color(theme.chrome),
        hovered || editing ? Color(theme.accent) : Color(theme.borderPanel),
        editing ? Color(theme.accent) : CLR_INVALID,
        editing ? 2.0F : 0.0F);
    if (fill >= 0.0F && !editing) {
        const int inner = std::max(0, static_cast<int>(rect.right - rect.left) - 4);
        const int width = static_cast<int>(std::lround(static_cast<float>(inner) * std::clamp(fill, 0.0F, 1.0F)));
        if (width > 0) {
            const COLORREF accent = Color(theme.accent);
            const COLORREF base = Color(theme.chrome);
            const auto mix = [&](int shift) { return static_cast<int>(((accent >> shift) & 0xFFU) * 30U + ((base >> shift) & 0xFFU) * 70U) / 100; };
            GdiDrawing::FillRectColor(dc, Rect(rect.left + 2, rect.bottom - 5, rect.left + 2 + width, rect.bottom - 2),
                RGB(mix(0), mix(8), mix(16)));
        }
    }
    ScopedFont font(12, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, font.handle);
    RECT textRect = Shrink(rect, 9, 0, 4, 0);
    if (!unit.empty()) {
        const int unitWidth = MeasureTextWidth(unit, 12, FW_NORMAL);
        const RECT unitRect = Rect(std::max(static_cast<int>(textRect.left), static_cast<int>(textRect.right) - unitWidth), textRect.top, textRect.right, textRect.bottom);
        Text(dc, unitRect, unit, Color(theme.textDisabled), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        textRect.right = std::max(textRect.left, unitRect.left - 6);
    }
    Text(dc, textRect, value, Color(theme.textPrimary));
    if (editing && caretVisible) {
        DrawTextCaret(dc, textRect, value, Color(theme.textPrimary));
    }
}

// The label of a row in its column. Its width is noted so that the column can grow to the widest label of the panel.
inline void DrawLabelText(HDC dc, RECT labelRect, const EditorTheme& theme, std::string_view label) {
    panel_style::NoteInspectorLabelWidth(MeasureTextWidth(label, 12, FW_SEMIBOLD));
    ScopedFont labelFont(12, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, labelFont.handle);
    Text(dc, labelRect, label, Color(theme.textSecondary));
}

// Defined below the vector rows, which use it for the reset arrow.
inline void DrawRowLabel(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section,
    InspectorPropertyId property, int index, std::string_view label);

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
    DrawValueBox(dc, box, theme, shown, state.IsHovered(InspectorHitKind::FloatField, section, property), editing,
        state.IsTextCaretVisible());
}

inline void DrawVec3Row(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, std::string_view label, const kb::scene::Vec3& value, InspectorPropertyId xProperty, InspectorPropertyId yProperty, InspectorPropertyId zProperty) {
    if (RowHovered(state, xProperty) || RowHovered(state, yProperty) || RowHovered(state, zProperty)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    const RECT valueRect = Rect(labelRect.right, row.top, row.right - kValueRightInset, row.bottom);
    DrawRowLabel(dc, row, theme, state, section, xProperty, -1, label);
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
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    const RECT valueRect = Rect(labelRect.right, row.top, row.right - kValueRightInset, row.bottom);
    DrawRowLabel(dc, row, theme, state, InspectorSectionId::Transform, InspectorPropertyId::RotationX, -1, label);
    const int valueWidth = static_cast<int>(valueRect.right - valueRect.left);
    const int laneWidth = std::max<int>(44, std::max(0, valueWidth - (kLaneGap * 2)) / 3);
    const int lanesWidth = laneWidth * 3 + kLaneGap * 2;
    const int laneLeft = static_cast<int>(valueRect.left) + std::max(0, (valueWidth - lanesWidth) / 2);
    const RECT x = Rect(laneLeft, valueRect.top, laneLeft + laneWidth, valueRect.bottom);
    const RECT y = Rect(x.right + kLaneGap, valueRect.top, x.right + kLaneGap + laneWidth, valueRect.bottom);
    const RECT z = Rect(y.right + kLaneGap, valueRect.top, y.right + kLaneGap + laneWidth, valueRect.bottom);
    const kb::math::Vec3 degrees = kb::math::ToEulerDegrees(value);
    DrawAxisLane(dc, x, theme, state, InspectorSectionId::Transform, InspectorPropertyId::RotationX, 'X', FormatFloat(degrees.x));
    DrawAxisLane(dc, y, theme, state, InspectorSectionId::Transform, InspectorPropertyId::RotationY, 'Y', FormatFloat(degrees.y));
    DrawAxisLane(dc, z, theme, state, InspectorSectionId::Transform, InspectorPropertyId::RotationZ, 'Z', FormatFloat(degrees.z));
}

// The label of a row. A value that differs from its default gives the right end of the label to a reset arrow; the
// arrow is where a click puts the value back (InspectorHitKind::ResetField).
[[nodiscard]] inline RECT ResetMarkerRect(RECT row) noexcept {
    const int right = InspectorLabelColumnRight(row);
    return Rect(right - kResetMarkerWidth, row.top, right, row.bottom);
}

inline void DrawRowLabel(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section,
    InspectorPropertyId property, int index, std::string_view label) {
    RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    const bool changed = state.IsFieldChanged(section, property, index);
    if (changed) {
        labelRect.right -= kResetMarkerWidth;
    }
    DrawLabelText(dc, labelRect, theme, label);
    if (changed) {
        const bool hovered = state.IsHovered(InspectorHitKind::ResetField, section, property, index);
        const RECT marker = ResetMarkerRect(row);
        const RECT icon = CenteredRect(marker, marker.left + 2, 14, 14);
        HeroIconPainter::Draw(dc, icon, HeroIconKind::Undo, hovered ? Color(theme.accent) : Color(theme.textDisabled), 1);
    }
}

// `editIndex` disambiguates rows that share one property id but are addressed by
// index (physics fields, script variables). Pass -1 for singular rows. The inline
// editor buffer is shown only in the specific row being edited, never in every
// sibling row that happens to reuse the same property id.
inline void DrawFieldRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, std::string_view label, std::string_view value, int editIndex = -1,
    std::string_view unit = {}, float fill = -1.0F) {
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
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    const RECT valueRect = Rect(labelRect.right, CenteredY(row, kValueHeight), row.right - kValueRightInset, CenteredY(row, kValueHeight) + kValueHeight);
    DrawRowLabel(dc, row, theme, state, section, property, editIndex, label);
    DrawValueBox(dc, valueRect, theme, shown, valueHovered, editing, state.IsTextCaretVisible(), unit, fill);
}

// A colour swatch filling a value box (sRGB values); alpha shows over a checkerboard.
inline void DrawColorSwatch(HDC dc, RECT value, const EditorTheme& theme, const std::array<float, 4U>& srgba, bool hovered) {
    DrawValueBox(dc, value, theme, {}, hovered);
    RECT fill = value;
    InflateRect(&fill, -2, -2);
    const float alpha = std::clamp(srgba[3], 0.0F, 1.0F);
    for (int y = fill.top; y < fill.bottom; y += 6) {
        for (int x = fill.left; x < fill.right; x += 6) {
            const float background = ((x - fill.left) / 6 + (y - fill.top) / 6) % 2 == 0 ? 0.65F : 0.4F;
            const auto channel = [&](int lane) {
                return static_cast<BYTE>(std::lround(255.0F *
                    (std::clamp(srgba[lane], 0.0F, 1.0F) * alpha + background * (1.0F - alpha))));
            };
            GdiDrawing::FillRectColor(dc, Rect(x, y, std::min(x + 6, static_cast<int>(fill.right)),
                std::min(y + 6, static_cast<int>(fill.bottom))), RGB(channel(0), channel(1), channel(2)));
        }
    }
}

// One colour of a scene component: label and a swatch that opens the colour picker. The
// component's colour is linear; the swatch shows it in sRGB.
inline void DrawColorRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section,
    InspectorPropertyId property, std::string_view label, const kb::scene::Vec3& linear) {
    const bool swatchHovered = state.IsHovered(InspectorHitKind::ColorField, section, property);
    GdiDrawing::FillRectColor(dc, row, RowHovered(state, property) || swatchHovered ? HoverFill(theme) : Color(theme.panel));
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    DrawRowLabel(dc, row, theme, state, section, property, -1, label);
    const RECT valueRect = Rect(labelRect.right, CenteredY(row, kValueHeight), row.right - kValueRightInset, CenteredY(row, kValueHeight) + kValueHeight);
    DrawColorSwatch(dc, valueRect, theme,
        { InspectorSceneColor::LinearToSrgb(linear.x), InspectorSceneColor::LinearToSrgb(linear.y), InspectorSceneColor::LinearToSrgb(linear.z), 1.0F },
        swatchHovered);
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
    DrawValueBox(dc, box, theme, shown, state.IsHovered(InspectorHitKind::FloatField, section, property), editing,
        state.IsTextCaretVisible());
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
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    const RECT valueRect = Rect(labelRect.right, row.top, row.right - kValueRightInset, row.bottom);
    DrawLabelText(dc, labelRect, theme, label);
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
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    const RECT valueRect = Rect(labelRect.right, row.top, row.right - kValueRightInset, row.bottom);
    DrawLabelText(dc, labelRect, theme, label);
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
        DrawInputFrame(
            dc,
            box,
            values[index] ? Color(theme.accent) : (hovered ? HoverFill(theme) : Color(theme.chrome)),
            values[index] || hovered ? Color(theme.accent) : Color(theme.borderPanel));
        if (values[index]) {
            HeroIconPainter::Draw(dc, Rect(box.left + 3, box.top + 3, box.right - 3, box.bottom - 3), HeroIconKind::Check,
                Color(theme.textPrimary), 1);
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
    const RECT button = Shrink(row, kRowPadX, 2, kValueRightInset, 2);
    DrawInputFrame(
        dc, button,
        hovered ? HoverFill(theme) : Color(theme.chrome),
        accent || hovered ? Color(theme.accent) : Color(theme.borderPanel));
    ScopedFont font(12, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, font.handle);
    Text(dc, button, label, accent ? Color(theme.textPrimary) : Color(theme.textSecondary),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// The box of a value chosen from a list that opens under the row: the chosen entry (dimmed when `chosen` is false, as
// for "None") and a chevron.
inline void DrawDropdownValue(HDC dc, RECT valueRect, const EditorTheme& theme, std::string_view text, bool chosen, bool hovered, bool open) {
    DrawInputFrame(dc, valueRect, hovered || open ? HoverFill(theme) : Color(theme.chrome),
        open ? Color(theme.textDisabled) : Color(theme.borderPanel));
    ScopedFont valueFont(12, chosen ? FW_SEMIBOLD : FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, valueFont.handle);
    Text(dc, Shrink(valueRect, 8, 0, 28, 0), text, chosen ? Color(theme.textPrimary) : Color(theme.textDisabled));
    constexpr int chevronSize = 12;
    const int chevronLeft = static_cast<int>(valueRect.right) - 15 - chevronSize / 2;
    const int chevronTop = CenteredY(valueRect, chevronSize);
    HeroIconPainter::Draw(dc, Rect(chevronLeft, chevronTop, chevronLeft + chevronSize, chevronTop + chevronSize),
        HeroIconKind::ChevronDown, Color(open ? theme.textPrimary : theme.textSecondary), 1);
}

inline void DrawTagFieldRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state,
    InspectorSectionId section, InspectorPropertyId property, std::string_view label, std::string_view value, int editIndex = -1) {
    if (RowHovered(state, property, editIndex)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    const RECT valueRect = Rect(labelRect.right, CenteredY(row, kValueHeight), row.right - kValueRightInset, CenteredY(row, kValueHeight) + kValueHeight);
    DrawLabelText(dc, labelRect, theme, label);

    const bool hovered = FieldValueHovered(state, section, property, editIndex) ||
        state.IsHovered(InspectorHitKind::ChoiceField, section, property, editIndex);
    const bool classified = !value.empty();
    const bool open = property == InspectorPropertyId::TagsText && state.IsTagsDropdownOpen();
    DrawDropdownValue(dc, valueRect, theme, classified ? value : EditorText("inspector.none", "None"), classified, hovered, open);
}

// A row whose value is picked from a list that opens right under it (the Joint's Preset): the label, and the chosen
// entry in a box with a chevron. A press on the row opens or closes the list.
inline void DrawDropdownRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section,
    InspectorPropertyId property, std::string_view label, std::string_view value, bool open) {
    const bool hovered = state.IsHovered(InspectorHitKind::Row, section, property) || state.IsHovered(InspectorHitKind::TextField, section, property);
    GdiDrawing::FillRectColor(dc, row, hovered ? HoverFill(theme) : Color(theme.panel));
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    const RECT valueRect = Rect(labelRect.right, CenteredY(row, kValueHeight), row.right - kValueRightInset, CenteredY(row, kValueHeight) + kValueHeight);
    DrawLabelText(dc, labelRect, theme, label);
    DrawDropdownValue(dc, valueRect, theme, value, true, hovered, open);
}

// One entry of such a list, in the value column under the row. The chosen one carries a check mark.
inline void DrawDropdownItemRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section,
    InspectorPropertyId property, int index, std::string_view label, bool chosen) {
    const bool hovered = state.IsHovered(InspectorHitKind::Row, section, property, index);
    GdiDrawing::FillRectColor(dc, row, Color(theme.panel));
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    const RECT box = Rect(labelRect.right, row.top + 2, row.right - kValueRightInset, row.bottom - 2);
    DrawInputFrame(dc, box, hovered ? HoverFill(theme) : Color(theme.chrome), hovered ? Color(theme.accent) : Color(theme.borderPanel));
    {
        ScopedFont font(12, chosen ? FW_SEMIBOLD : FW_NORMAL);
        const ScopedGdiObject selectedFont(dc, font.handle);
        Text(dc, Shrink(box, 8, 0, 28, 0), label, hovered || chosen ? Color(theme.textPrimary) : Color(theme.textSecondary));
    }
    if (chosen) {
        constexpr int markSize = 12;
        const int left = static_cast<int>(box.right) - 15 - markSize / 2;
        const int top = CenteredY(box, markSize);
        HeroIconPainter::Draw(dc, Rect(left, top, left + markSize, top + markSize), HeroIconKind::Check, Color(theme.accent), 1);
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

inline void DrawAssetFieldRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, InspectorPropertyId buttonProperty, std::string_view label, std::string_view value, int editIndex = -1) {
    if (RowHovered(state, property, editIndex) || state.IsHovered(InspectorHitKind::TextField, section, buttonProperty, editIndex)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    const RECT valueRect = Rect(labelRect.right, CenteredY(row, kValueHeight), row.right - kValueRightInset, CenteredY(row, kValueHeight) + kValueHeight);
    const RECT textRect = AssetPickerTextRect(valueRect);
    const bool valueHovered = state.IsHovered(InspectorHitKind::TextField, section, property, editIndex);
    const bool buttonHovered = state.IsHovered(InspectorHitKind::TextField, section, buttonProperty, editIndex);
    DrawLabelText(dc, labelRect, theme, label);
    DrawValueBox(dc, textRect, theme, value, valueHovered);
    const RECT button = AssetPickerButtonRect(valueRect);
    DrawInputFrame(dc, button, buttonHovered ? HoverFill(theme) : Color(theme.chrome), buttonHovered ? Color(theme.accent) : Color(theme.borderPanel));
    HeroIconPainter::Draw(dc, Shrink(button, 3, 3, 3, 3), HeroIconKind::MagnifyingGlass, buttonHovered ? Color(theme.textPrimary) : Color(theme.textSecondary), 1);
}

// One entry of a list that opens under a section (the component menu, the Joint's presets): a full-width
// button row. A disabled entry stays visible and takes no click.
inline void DrawMenuItemRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section,
    InspectorPropertyId property, int index, std::string_view label, bool enabled) {
    const bool hovered = enabled && state.IsHovered(InspectorHitKind::Row, section, property, index);
    GdiDrawing::FillRectColor(dc, row, Color(theme.panel));
    const RECT button = Shrink(row, kRowPadX, 2, kValueRightInset, 2);
    DrawInputFrame(dc, button, hovered ? HoverFill(theme) : Color(theme.chrome), hovered ? Color(theme.accent) : Color(theme.borderPanel));
    ScopedFont font(12, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, font.handle);
    Text(dc, button, label, !enabled ? Color(theme.textDisabled) : hovered ? Color(theme.textPrimary) : Color(theme.textSecondary),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

[[nodiscard]] inline std::string_view ComponentMenuItemLabel(InspectorComponentMenuItem item) {
    switch (item) {
    case InspectorComponentMenuItem::Reset: return EditorText("inspector.menu.reset", "Reset");
    case InspectorComponentMenuItem::Remove: return EditorText("inspector.menu.remove", "Remove Component");
    case InspectorComponentMenuItem::Copy: return EditorText("inspector.menu.copy", "Copy Settings");
    case InspectorComponentMenuItem::Paste: return EditorText("inspector.menu.paste", "Paste Settings");
    case InspectorComponentMenuItem::ResetPosition: return EditorText("inspector.menu.resetPosition", "Reset Position");
    case InspectorComponentMenuItem::ResetRotation: return EditorText("inspector.menu.resetRotation", "Reset Rotation");
    case InspectorComponentMenuItem::ResetScale: return EditorText("inspector.menu.resetScale", "Reset Scale");
    }
    return {};
}

// The height the open list under a section adds; sections add it to their own.
[[nodiscard]] inline int OpenMenuHeight(const InspectorPanelState& state, InspectorSectionId section) noexcept {
    return state.IsComponentMenuOpen(section) && !state.IsCollapsed(section)
        ? static_cast<int>(InspectorComponentMenuItems(section).size()) * (kFieldRowHeight + kDividerHeight) : 0;
}

// One element of the Dropdown's Options list, first line: a drag handle and the option's label field.
[[nodiscard]] inline RECT DropdownOptionHandleRect(RECT row) noexcept {
    return Rect(row.left + kRowPadX, row.top, row.left + kRowPadX + 16, row.bottom);
}

[[nodiscard]] inline RECT DropdownOptionTextRect(RECT row) noexcept {
    const int top = CenteredY(row, kValueHeight);
    return Rect(row.left + kRowPadX + 22, top, row.right - kValueRightInset, top + kValueHeight);
}

// The list footer's + and - buttons, right-aligned under the elements.
[[nodiscard]] inline RECT DropdownOptionFooterButtonRect(RECT row, bool remove) noexcept {
    constexpr int kButtonWidth = 26;
    const int top = CenteredY(row, kValueHeight);
    const int right = row.right - kValueRightInset - (remove ? 0 : kButtonWidth + 4);
    return Rect(right - kButtonWidth, top, right, top + kValueHeight);
}

inline void DrawDropdownOptionTextRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state,
    InspectorSectionId section, InspectorPropertyId textProperty, int textRow, int option, std::string_view value, bool selected) {
    GdiDrawing::FillRectColor(dc, row, selected ? HoverFill(theme) : Color(theme.panel));
    const bool handleHovered = state.IsHovered(InspectorHitKind::Row, section, InspectorPropertyId::UIDropdownOptionHandle, option);
    ScopedFont handleFont(13, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedFont(dc, handleFont.handle);
        Text(dc, DropdownOptionHandleRect(row), "\xE2\x89\xA1", handleHovered ? Color(theme.textPrimary) : Color(theme.textSecondary),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    const bool editing = state.EditedProperty() == textProperty && state.EditIndex() == textRow;
    DrawValueBox(dc, DropdownOptionTextRect(row), theme, editing ? std::string_view{state.EditBuffer()} : value,
        state.IsHovered(InspectorHitKind::TextField, section, textProperty, textRow), editing, state.IsTextCaretVisible());
}

inline void DrawDropdownOptionFooter(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state,
    InspectorSectionId section, bool canAdd, bool canRemove) {
    GdiDrawing::FillRectColor(dc, row, Color(theme.panel));
    ScopedFont font(14, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, font.handle);
    for (const bool remove : { false, true }) {
        const RECT box = DropdownOptionFooterButtonRect(row, remove);
        const InspectorPropertyId property = remove ? InspectorPropertyId::UIDropdownOptionRemove : InspectorPropertyId::UIDropdownOptionAdd;
        const bool enabled = remove ? canRemove : canAdd;
        const bool hovered = enabled && state.IsHovered(InspectorHitKind::Row, section, property, -1);
        DrawInputFrame(dc, box, hovered ? HoverFill(theme) : Color(theme.chrome), hovered ? Color(theme.accent) : Color(theme.borderPanel));
        Text(dc, box, remove ? "-" : "+", !enabled ? Color(theme.textDisabled) : hovered ? Color(theme.textPrimary) : Color(theme.textSecondary),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

[[nodiscard]] inline RECT CheckboxRectForRow(RECT row) noexcept {
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, InspectorLabelColumnRight(row), row.bottom);
    return CenteredRect(row, labelRect.right, kCheckboxSize, kCheckboxSize);
}

inline void DrawBoolRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, std::string_view label, bool checked, int editIndex = -1) {
    if (RowHovered(state, property, editIndex)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT box = CheckboxRectForRow(row);
    DrawRowLabel(dc, row, theme, state, section, property, editIndex, label);
    const bool hovered = state.IsHovered(InspectorHitKind::BoolField, section, property, editIndex);
    DrawInputFrame(
        dc,
        box,
        checked ? Color(theme.accent) : (hovered ? HoverFill(theme) : Color(theme.chrome)),
        checked || hovered ? Color(theme.accent) : Color(theme.borderPanel));
    if (checked) {
        HeroIconPainter::Draw(dc, Rect(box.left + 3, box.top + 3, box.right - 3, box.bottom - 3), HeroIconKind::Check,
            Color(theme.textPrimary), 1);
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
            if (state_.IsComponentMenuOpen(section_)) {
                int item = 0;
                for (const InspectorComponentMenuItem which : InspectorComponentMenuItems(section_)) {
                    const bool enabled = which != InspectorComponentMenuItem::Paste || InspectorComponentDefaults::CanPaste(section_);
                    DrawMenuItemRow(dc_, Row(), theme_, state_, section_, InspectorPropertyId::ComponentMenuItem, item, ComponentMenuItemLabel(which), enabled);
                    Advance();
                    ++item;
                }
            }
        }
    }

    ~SectionWriter() {
        DrawSectionCardOutline(
            dc_,
            Rect(bounds_.left, bounds_.top, bounds_.right, y_),
            theme_);
    }

    void Field(std::string_view label, std::string_view value, InspectorPropertyId property = InspectorPropertyId::None, int editIndex = -1, std::string_view unit = {}, float fill = -1.0F) { if (!collapsed_) { DrawFieldRow(dc_, Row(), theme_, state_, section_, property, label, value, editIndex, unit, fill); Advance(); } }
    // A number: its unit and, when it has a natural range, the slider fill come from the field guide.
    void Float(std::string_view label, std::string_view value, InspectorPropertyId property, float number = std::numeric_limits<float>::quiet_NaN()) {
        const InspectorFieldHelp help = InspectorFieldGuide::Help(property);
        const float span = help.maximum - help.minimum;
        const float fill = help.bounded && span > 0.0F && !std::isnan(number) ? (number - help.minimum) / span : -1.0F;
        Field(label, value, property, -1, help.unit, fill);
    }
    void Tag(std::string_view label, std::string_view value, InspectorPropertyId property, int editIndex = -1) { if (!collapsed_) { DrawTagFieldRow(dc_, Row(), theme_, state_, section_, property, label, value, editIndex); Advance(); } }
    void AssetField(std::string_view label, std::string_view value, InspectorPropertyId property, InspectorPropertyId buttonProperty, int editIndex = -1) { if (!collapsed_) { DrawAssetFieldRow(dc_, Row(), theme_, state_, section_, property, buttonProperty, label, value, editIndex); Advance(); } }
    void Color(const kb::scene::Vec3& linear, InspectorPropertyId property) { if (!collapsed_) { DrawColorRow(dc_, Row(), theme_, state_, section_, property, InspectorSceneColor::Label(property), linear); Advance(); } }
    void Bool(std::string_view label, bool value, InspectorPropertyId property = InspectorPropertyId::None, int editIndex = -1) { if (!collapsed_) { DrawBoolRow(dc_, Row(), theme_, state_, section_, property, label, value, editIndex); Advance(); } }
    void Vec3(std::string_view label, const kb::scene::Vec3& value, InspectorPropertyId x, InspectorPropertyId y, InspectorPropertyId z) { if (!collapsed_) { DrawVec3Row(dc_, Row(), theme_, state_, section_, label, value, x, y, z); Advance(); } }
    void Pair(std::string_view label, std::string_view firstLabel, std::string_view firstValue, InspectorPropertyId firstProperty, std::string_view secondLabel, std::string_view secondValue, InspectorPropertyId secondProperty) { if (!collapsed_) { DrawPairRow(dc_, Row(), theme_, state_, section_, label, firstLabel, firstValue, firstProperty, secondLabel, secondValue, secondProperty); Advance(); } }
    void BoolPair(std::string_view label, std::string_view firstLabel, bool firstValue, InspectorPropertyId firstProperty, std::string_view secondLabel, bool secondValue, InspectorPropertyId secondProperty) { if (!collapsed_) { DrawBoolPairRow(dc_, Row(), theme_, state_, section_, label, firstLabel, firstValue, firstProperty, secondLabel, secondValue, secondProperty); Advance(); } }
    void Group(std::string_view label) { if (!collapsed_) { DrawGroupRow(dc_, Rect(bounds_.left, y_, bounds_.right, y_ + kGroupRowHeight), theme_, label); y_ += kGroupRowHeight; DrawDivider(dc_, theme_, bounds_.left, bounds_.right, y_); y_ += kDividerHeight; } }
    void Action(std::string_view label, InspectorPropertyId property, bool accent = false) { if (!collapsed_) { DrawActionRow(dc_, Row(), theme_, state_, section_, property, label, accent); Advance(); } }
    void MenuItem(std::string_view label, InspectorPropertyId property, int index, bool enabled = true) { if (!collapsed_) { DrawMenuItemRow(dc_, Row(), theme_, state_, section_, property, index, label, enabled); Advance(); } }
    void Dropdown(std::string_view label, std::string_view value, InspectorPropertyId property, bool open) { if (!collapsed_) { DrawDropdownRow(dc_, Row(), theme_, state_, section_, property, label, value, open); Advance(); } }
    void DropdownItem(std::string_view label, InspectorPropertyId property, int index, bool chosen) { if (!collapsed_) { DrawDropdownItemRow(dc_, Row(), theme_, state_, section_, property, index, label, chosen); Advance(); } }
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
