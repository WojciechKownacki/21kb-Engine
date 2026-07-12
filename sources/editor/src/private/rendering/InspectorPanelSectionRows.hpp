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
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace kb::editor::inspector_panel_rows {
namespace {

constexpr int kSectionHeaderHeight = 24;
constexpr int kFieldRowHeight = 24;
constexpr int kValueHeight = 20;
constexpr int kRowPadX = 16;
constexpr int kValuePadX = 10;
constexpr int kAssetPickerButtonSize = 18;
constexpr int kAssetPickerButtonGap = 3;
constexpr int kAxisLetterWidth = 11;
constexpr int kAxisGap = 6;
constexpr int kLaneGap = 5;
constexpr int kDividerHeight = 1;
constexpr int kCheckboxSize = 16;
constexpr int kComponentMenuButtonSize = 18;
constexpr int kTextBaselineOffsetY = 1;

[[nodiscard]] inline COLORREF Color(EditorColor color) {
    return GdiDrawing::ToColorRef(color);
}

[[nodiscard]] inline COLORREF Rgb(int red, int green, int blue) noexcept {
    return RGB(red, green, blue);
}

[[nodiscard]] inline COLORREF HoverFill(const EditorTheme&) noexcept {
    return Rgb(34, 38, 45);
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

[[nodiscard]] inline bool RowHovered(const InspectorPanelState& state, InspectorPropertyId property) noexcept {
    return property != InspectorPropertyId::None && state.HoveredProperty() == property;
}

inline void Text(HDC dc, RECT rect, std::string_view text, COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    rect.top += kTextBaselineOffsetY;
    rect.bottom += kTextBaselineOffsetY;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
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

inline void DrawDivider(HDC dc, int left, int right, int y) {
    GdiDrawing::FillRectColor(dc, Rect(left, y, right, y + kDividerHeight), Rgb(0, 0, 0));
}

inline void DrawTriangle(HDC dc, RECT rect, bool expanded, COLORREF color) {
    ProjectFilesPanelDrawing::DrawDisclosureTriangle(dc, rect, color, expanded);
}

[[nodiscard]] inline RECT ComponentRemoveButtonRect(RECT header) noexcept {
    const int top = CenteredY(header, kComponentMenuButtonSize);
    return Rect(header.right - kRowPadX - kComponentMenuButtonSize, top, header.right - kRowPadX, top + kComponentMenuButtonSize);
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

inline void DrawSectionHeader(HDC dc, RECT rect, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, HeroIconKind icon, std::string_view title, bool removeButton = false) {
    const bool hovered = state.IsHovered(InspectorHitKind::SectionHeader, section, InspectorPropertyId::None);
    GdiDrawing::FillRectColor(dc, rect, hovered ? HoverFill(theme) : Color(theme.strip));

    RECT chevron = Rect(rect.left + 9, rect.top, rect.left + 29, rect.bottom);
    DrawTriangle(dc, Shrink(chevron, 4, 4, 4, 4), !state.IsCollapsed(section), Color(theme.textSecondary));
    const RECT iconRect = Rect(rect.left + 35, rect.top + 3, rect.left + 53, rect.top + 21);
    HeroIconPainter::Draw(dc, iconRect, icon, Color(theme.textSecondary), 2);

    const RECT titleRect = Rect(rect.left + 59, rect.top, removeButton ? rect.right - 44 : rect.right - 8, rect.bottom);
    ScopedFont font(13, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, font.handle);
    Text(dc, titleRect, title, Color(theme.textPrimary));

    if (removeButton) {
        const RECT button = ComponentRemoveButtonRect(rect);
        const bool buttonHovered = state.IsHovered(InspectorHitKind::ComponentMenuButton, section, InspectorPropertyId::ComponentRemove);
        DrawFrame(dc, button, buttonHovered ? HoverFill(theme) : Color(theme.strip), Color(theme.borderPanel));
        ScopedFont xFont(11, FW_SEMIBOLD);
        const ScopedGdiObject selectedXFont(dc, xFont.handle);
        RECT glyph = button;
        glyph.top -= 1;
        glyph.bottom -= 1;
        Text(dc, glyph, "x", Color(theme.textSecondary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

inline void DrawValueBox(HDC dc, RECT rect, const EditorTheme& theme, std::string_view value, bool hovered = false) {
    DrawFrame(dc, rect, hovered ? HoverFill(theme) : Color(theme.chrome), Color(theme.borderPanel));
    ScopedFont valueFont(12, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, valueFont.handle);
    Text(dc, Shrink(rect, kValuePadX, 0, 4, 0), value, Color(theme.textPrimary));
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

inline void DrawFieldRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, std::string_view label, std::string_view value) {
    if (RowHovered(state, property)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    const RECT valueRect = Rect(labelRect.right, CenteredY(row, kValueHeight), row.right - kRowPadX, CenteredY(row, kValueHeight) + kValueHeight);
    const bool editing = property != InspectorPropertyId::None && state.EditedProperty() == property;
    const std::string_view shown = editing ? std::string_view{ state.EditBuffer() } : value;
    ScopedFont labelFont(12, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }
    DrawValueBox(dc, valueRect, theme, shown, state.IsHovered(InspectorHitKind::TextField, section, property) || state.IsHovered(InspectorHitKind::FloatField, section, property) || editing);
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

inline void DrawBoolRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, std::string_view label, bool checked) {
    if (RowHovered(state, property)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    const RECT box = CheckboxRectForRow(row);
    ScopedFont labelFont(12, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }
    const bool hovered = state.IsHovered(InspectorHitKind::BoolField, section, property);
    DrawFrame(dc, box, hovered ? HoverFill(theme) : Color(theme.chrome), Color(theme.borderPanel));
    if (checked) {
        ScopedFont markFont(10, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, markFont.handle);
        RECT glyph = box;
        glyph.top -= 1;
        glyph.bottom -= 1;
        TextW(dc, glyph, L"\u2714", Color(theme.textPrimary));
    }
}

class SectionWriter {
public:
    SectionWriter(HDC dc, RECT bounds, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, HeroIconKind icon, std::string_view title, bool menuButton = false)
        : dc_(dc), bounds_(bounds), theme_(theme), state_(state), section_(section), collapsed_(state.IsCollapsed(section)), y_(bounds.top) {
        DrawSectionHeader(dc_, Rect(bounds_.left, y_, bounds_.right, y_ + kSectionHeaderHeight), theme_, state_, section_, icon, title, menuButton);
        y_ += kSectionHeaderHeight;
        if (!collapsed_) {
            DrawDivider(dc_, bounds_.left, bounds_.right, y_);
            y_ += kDividerHeight;
        }
    }

    void Field(std::string_view label, std::string_view value, InspectorPropertyId property = InspectorPropertyId::None) { if (!collapsed_) { DrawFieldRow(dc_, Row(), theme_, state_, section_, property, label, value); Advance(); } }
    void AssetField(std::string_view label, std::string_view value, InspectorPropertyId property, InspectorPropertyId buttonProperty) { if (!collapsed_) { DrawAssetFieldRow(dc_, Row(), theme_, state_, section_, property, buttonProperty, label, value); Advance(); } }
    void Float(std::string_view label, std::string_view value, InspectorPropertyId property) { Field(label, value, property); }
    void Bool(std::string_view label, bool value, InspectorPropertyId property = InspectorPropertyId::None) { if (!collapsed_) { DrawBoolRow(dc_, Row(), theme_, state_, section_, property, label, value); Advance(); } }
    void Vec3(std::string_view label, const kb::scene::Vec3& value, InspectorPropertyId x, InspectorPropertyId y, InspectorPropertyId z) { if (!collapsed_) { DrawVec3Row(dc_, Row(), theme_, state_, section_, label, value, x, y, z); Advance(); } }
    void Rotation(std::string_view label, const kb::scene::Quat& value) { if (!collapsed_) { DrawRotationRow(dc_, Row(), theme_, state_, label, value); Advance(); } }
    [[nodiscard]] int Bottom() const noexcept { return y_; }

private:
    [[nodiscard]] RECT Row() const noexcept { return Rect(bounds_.left, y_, bounds_.right, y_ + kFieldRowHeight); }
    void Advance() { y_ += kFieldRowHeight; DrawDivider(dc_, bounds_.left, bounds_.right, y_); y_ += kDividerHeight; }
    HDC dc_ = nullptr;
    RECT bounds_{};
    const EditorTheme& theme_;
    const InspectorPanelState& state_;
    InspectorSectionId section_ = InspectorSectionId::None;
    bool collapsed_ = false;
    int y_ = 0;
};

} // namespace kb::editor::inspector_panel_rows
