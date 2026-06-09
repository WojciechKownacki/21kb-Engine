#include "rendering/InspectorPanelRenderer.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "inspection/InspectorComponentLabelFormatter.hpp"
#include "rendering/EditorMeshPreviewService.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/GdiResources.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/gdi/ScopedBrush.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/gdi/ScopedPen.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {
namespace {

constexpr int kHeaderHeight = 62;
constexpr int kHeaderIcon = 36;
constexpr int kHeaderPad = 8;
constexpr int kPanelPadTop = 10;
constexpr int kSectionGap = 8;
constexpr int kSectionHeaderHeight = 24;
constexpr int kFieldRowHeight = 24;
constexpr int kValueHeight = 20;
constexpr int kRowPadX = 16;
constexpr int kValuePadX = 10;
constexpr int kAxisLetterWidth = 11;
constexpr int kAxisGap = 6;
constexpr int kLaneGap = 5;
constexpr int kDividerHeight = 1;
constexpr int kCheckboxSize = 16;
constexpr int kTextBaselineOffsetY = 1;
constexpr int kAssetPreviewMaxHeight = 214;
constexpr int kAssetPreviewMinHeight = 142;
constexpr int kMeshPreviewToolbarHeight = 30;
constexpr int kMeshPreviewToolbarButtonSize = 22;
constexpr int kMeshPreviewToolbarButtonGap = 4;
constexpr float kMeshPreviewFitZoom = 1.35F;

[[nodiscard]] COLORREF Color(EditorColor color) {
    return GdiDrawing::ToColorRef(color);
}

[[nodiscard]] COLORREF Rgb(int r, int g, int b) noexcept {
    return RGB(r, g, b);
}

[[nodiscard]] COLORREF HoverFill(const EditorTheme& theme) noexcept {
    static_cast<void>(theme);
    return Rgb(34, 38, 45);
}

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept {
    return RECT{ left, top, right, bottom };
}

[[nodiscard]] RECT Shrink(RECT rect, int left, int top, int right, int bottom) noexcept {
    rect.left += left;
    rect.top += top;
    rect.right -= right;
    rect.bottom -= bottom;
    return rect;
}

[[nodiscard]] int CenteredY(const RECT& outer, int height) noexcept {
    return static_cast<int>(outer.top) + std::max(0, (static_cast<int>(outer.bottom - outer.top) - height) / 2);
}

[[nodiscard]] RECT CenteredRect(const RECT& outer, int left, int width, int height) noexcept {
    const int top = CenteredY(outer, height);
    return Rect(left, top, left + width, top + height);
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] std::string FormatFloat(float value, int precision = 3) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), precision == 0 ? "%.0f" : precision == 1 ? "%.1f" : precision == 2 ? "%.2f" : "%.3f", static_cast<double>(value));
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text == "-0" ? "0" : text;
}

[[nodiscard]] std::string FormatUInt64(std::uint64_t value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    return buffer;
}

[[nodiscard]] std::string FormatVec3(const float value[3]) {
    return FormatFloat(value[0], 2) + ", " + FormatFloat(value[1], 2) + ", " + FormatFloat(value[2], 2);
}

[[nodiscard]] std::string NormalizePath(const std::filesystem::path& path) {
    return kb::assets::NormalizeAssetPath(path);
}

void Text(HDC dc, RECT rect, std::string_view text, COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    rect.top += kTextBaselineOffsetY;
    rect.bottom += kTextBaselineOffsetY;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
}

void TextW(HDC dc, RECT rect, std::wstring_view text, COLORREF color, UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
    rect.top += kTextBaselineOffsetY;
    rect.bottom += kTextBaselineOffsetY;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
}

void DrawFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
}

void DrawThumbnailBitmap(HDC dc, const RECT& target, const EditorMeshThumbnailImage& image) {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty() || target.right <= target.left || target.bottom <= target.top) {
        return;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const int oldMode = SetStretchBltMode(dc, HALFTONE);
    static_cast<void>(StretchDIBits(
        dc,
        target.left,
        target.top,
        static_cast<int>(target.right - target.left),
        static_cast<int>(target.bottom - target.top),
        0,
        0,
        image.width,
        image.height,
        image.bgra.data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY));
    SetStretchBltMode(dc, oldMode);
}

void DrawDivider(HDC dc, int left, int right, int y) {
    GdiDrawing::FillRectColor(dc, Rect(left, y, right, y + kDividerHeight), Rgb(0, 0, 0));
}

[[nodiscard]] bool RowHovered(const InspectorPanelState& state, InspectorPropertyId property) noexcept {
    return property != InspectorPropertyId::None && state.HoveredProperty() == property;
}

void DrawTriangle(HDC dc, RECT rect, bool expanded, COLORREF color) {
    ProjectFilesPanelDrawing::DrawDisclosureTriangle(dc, rect, color, expanded);
}

void DrawSectionHeader(HDC dc, RECT rect, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, HeroIconKind icon, std::string_view title) {
    const bool hovered = state.IsHovered(InspectorHitKind::SectionHeader, section, InspectorPropertyId::None);
    GdiDrawing::FillRectColor(dc, rect, hovered ? HoverFill(theme) : Color(theme.strip));

    RECT chevron = Rect(rect.left + 9, rect.top, rect.left + 29, rect.bottom);
    DrawTriangle(dc, Shrink(chevron, 4, 4, 4, 4), !state.IsCollapsed(section), Color(theme.textSecondary));

    RECT iconRect = Rect(rect.left + 35, rect.top + 3, rect.left + 53, rect.top + 21);
    HeroIconPainter::Draw(dc, iconRect, icon, Color(theme.textSecondary), 2);

    RECT titleRect = Rect(rect.left + 59, rect.top, rect.right - 8, rect.bottom);
    ScopedFont font(13, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, font.handle);
    Text(dc, titleRect, title, Color(theme.textPrimary));
}

void DrawValueBox(HDC dc, RECT rect, const EditorTheme& theme, std::string_view value, bool hovered = false) {
    DrawFrame(dc, rect, hovered ? HoverFill(theme) : Color(theme.chrome), Color(theme.borderPanel));
    ScopedFont valueFont(12, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, valueFont.handle);
    Text(dc, Shrink(rect, kValuePadX, 0, 4, 0), value, Color(theme.textPrimary));
}

[[nodiscard]] COLORREF AxisColor(char axis) noexcept {
    switch (axis) {
    case 'X':
        return Rgb(255, 66, 47);
    case 'Y':
        return Rgb(36, 123, 255);
    case 'Z':
        return Rgb(90, 216, 57);
    default:
        return Rgb(178, 184, 199);
    }
}

void DrawAxisLane(HDC dc, RECT rect, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, char axis, std::string_view value) {
    RECT letter = Rect(rect.left, rect.top, rect.left + kAxisLetterWidth, rect.bottom);
    {
        ScopedFont font(11, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, font.handle);
        char label[2]{ axis, '\0' };
        Text(dc, letter, label, AxisColor(axis), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    const bool editing = property != InspectorPropertyId::None && state.EditedProperty() == property;
    const std::string_view shown = editing ? std::string_view{ state.EditBuffer() } : value;
    RECT box = Rect(letter.right + kAxisGap, CenteredY(rect, kValueHeight), rect.right, CenteredY(rect, kValueHeight) + kValueHeight);
    DrawValueBox(dc, box, theme, shown, state.IsHovered(InspectorHitKind::FloatField, section, property) || editing);
}

void DrawVec3Row(
    HDC dc,
    RECT row,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    InspectorSectionId section,
    std::string_view label,
    const kb::scene::Vec3& value,
    InspectorPropertyId xProperty,
    InspectorPropertyId yProperty,
    InspectorPropertyId zProperty) {
    if (RowHovered(state, xProperty) || RowHovered(state, yProperty) || RowHovered(state, zProperty)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    RECT valueRect = Rect(labelRect.right, row.top, row.right - kRowPadX, row.bottom);

    ScopedFont labelFont(12, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }

    const int valueWidth = static_cast<int>(valueRect.right - valueRect.left);
    const int available = std::max(0, valueWidth - (kLaneGap * 2));
    const int laneWidth = std::max<int>(44, available / 3);
    const int lanesWidth = laneWidth * 3 + kLaneGap * 2;
    const int laneLeft = static_cast<int>(valueRect.left) + std::max(0, (valueWidth - lanesWidth) / 2);
    RECT x = Rect(laneLeft, valueRect.top, laneLeft + laneWidth, valueRect.bottom);
    RECT y = Rect(x.right + kLaneGap, valueRect.top, x.right + kLaneGap + laneWidth, valueRect.bottom);
    RECT z = Rect(y.right + kLaneGap, valueRect.top, y.right + kLaneGap + laneWidth, valueRect.bottom);
    DrawAxisLane(dc, x, theme, state, section, xProperty, 'X', FormatFloat(value.x));
    DrawAxisLane(dc, y, theme, state, section, yProperty, 'Y', FormatFloat(value.y));
    DrawAxisLane(dc, z, theme, state, section, zProperty, 'Z', FormatFloat(value.z));
}

void DrawRotationRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, std::string_view label, const kb::scene::Quat& value) {
    if (RowHovered(state, InspectorPropertyId::RotationX) || RowHovered(state, InspectorPropertyId::RotationY) || RowHovered(state, InspectorPropertyId::RotationZ)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    RECT valueRect = Rect(labelRect.right, row.top, row.right - kRowPadX, row.bottom);
    ScopedFont labelFont(12, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }

    const int valueWidth = static_cast<int>(valueRect.right - valueRect.left);
    const int available = std::max(0, valueWidth - (kLaneGap * 2));
    const int laneWidth = std::max<int>(44, available / 3);
    const int lanesWidth = laneWidth * 3 + kLaneGap * 2;
    const int laneLeft = static_cast<int>(valueRect.left) + std::max(0, (valueWidth - lanesWidth) / 2);
    RECT x = Rect(laneLeft, valueRect.top, laneLeft + laneWidth, valueRect.bottom);
    RECT y = Rect(x.right + kLaneGap, valueRect.top, x.right + kLaneGap + laneWidth, valueRect.bottom);
    RECT z = Rect(y.right + kLaneGap, valueRect.top, y.right + kLaneGap + laneWidth, valueRect.bottom);
    DrawAxisLane(dc, x, theme, state, InspectorSectionId::Transform, InspectorPropertyId::RotationX, 'X', FormatFloat(value.x));
    DrawAxisLane(dc, y, theme, state, InspectorSectionId::Transform, InspectorPropertyId::RotationY, 'Y', FormatFloat(value.y));
    DrawAxisLane(dc, z, theme, state, InspectorSectionId::Transform, InspectorPropertyId::RotationZ, 'Z', FormatFloat(value.z));
}

void DrawFieldRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, std::string_view label, std::string_view value) {
    if (RowHovered(state, property)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    RECT valueRect = Rect(labelRect.right, CenteredY(row, kValueHeight), row.right - kRowPadX, CenteredY(row, kValueHeight) + kValueHeight);
    const bool editing = property != InspectorPropertyId::None && state.EditedProperty() == property;
    const std::string_view shown = editing ? std::string_view{ state.EditBuffer() } : value;

    ScopedFont labelFont(12, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, labelRect, label, Color(theme.textSecondary));
    }
    DrawValueBox(dc, valueRect, theme, shown, state.IsHovered(InspectorHitKind::TextField, section, property) || state.IsHovered(InspectorHitKind::FloatField, section, property) || editing);
}

[[nodiscard]] RECT CheckboxRectForRow(RECT row) noexcept {
    const RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    return CenteredRect(row, labelRect.right, kCheckboxSize, kCheckboxSize);
}

void DrawBoolRow(HDC dc, RECT row, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, InspectorPropertyId property, std::string_view label, bool checked) {
    if (RowHovered(state, property)) {
        GdiDrawing::FillRectColor(dc, row, HoverFill(theme));
    }
    RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    RECT box = CheckboxRectForRow(row);
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
    SectionWriter(HDC dc, RECT bounds, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, HeroIconKind icon, std::string_view title)
        : dc_(dc), bounds_(bounds), theme_(theme), state_(state), section_(section), collapsed_(state.IsCollapsed(section)) {
        DrawSectionHeader(dc_, Rect(bounds_.left, y_, bounds_.right, y_ + kSectionHeaderHeight), theme_, state_, section_, icon, title);
        y_ += kSectionHeaderHeight;
        if (collapsed_) {
            return;
        }
        DrawDivider(dc_, bounds_.left, bounds_.right, y_);
        y_ += kDividerHeight;
    }

    void Field(std::string_view label, std::string_view value, InspectorPropertyId property = InspectorPropertyId::None) {
        if (collapsed_) {
            return;
        }
        DrawFieldRow(dc_, Row(), theme_, state_, section_, property, label, value);
        Advance();
    }

    void Bool(std::string_view label, bool value, InspectorPropertyId property = InspectorPropertyId::None) {
        if (collapsed_) {
            return;
        }
        DrawBoolRow(dc_, Row(), theme_, state_, section_, property, label, value);
        Advance();
    }

    void Vec3(std::string_view label, const kb::scene::Vec3& value, InspectorPropertyId x, InspectorPropertyId y, InspectorPropertyId z) {
        if (collapsed_) {
            return;
        }
        DrawVec3Row(dc_, Row(), theme_, state_, section_, label, value, x, y, z);
        Advance();
    }

    void Rotation(std::string_view label, const kb::scene::Quat& value) {
        if (collapsed_) {
            return;
        }
        DrawRotationRow(dc_, Row(), theme_, state_, label, value);
        Advance();
    }

    [[nodiscard]] int Bottom() const noexcept {
        return y_;
    }

private:
    [[nodiscard]] RECT Row() const noexcept {
        return Rect(bounds_.left, y_, bounds_.right, y_ + kFieldRowHeight);
    }

    void Advance() {
        y_ += kFieldRowHeight;
        DrawDivider(dc_, bounds_.left, bounds_.right, y_);
        y_ += kDividerHeight;
    }

    HDC dc_ = nullptr;
    RECT bounds_{};
    const EditorTheme& theme_;
    const InspectorPanelState& state_;
    InspectorSectionId section_ = InspectorSectionId::None;
    bool collapsed_ = false;
    int y_ = bounds_.top;
};

[[nodiscard]] std::optional<std::filesystem::path> ResolveAssetPhysicalPath(const kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata) {
    if (!metadata.physicalPath.empty()) {
        return metadata.physicalPath;
    }
    return manager.Mounts().Resolve(metadata.virtualPath);
}

[[nodiscard]] EditorMeshPreviewSettings MeshPreviewSettingsFromState(const InspectorPanelState& inspector) noexcept {
    return EditorMeshPreviewSettings{
        .yawDegrees = inspector.MeshPreviewYaw(),
        .pitchDegrees = inspector.MeshPreviewPitch(),
        .zoom = inspector.MeshPreviewZoom(),
        .renderMode = inspector.MeshPreviewRenderMode(),
        .lightPreset = inspector.MeshPreviewLightPreset(),
    };
}

[[nodiscard]] RECT MeshPreviewToolbarRect(const RECT& panel) noexcept {
    return Rect(panel.left + 8, panel.top + 7, panel.right - 8, panel.top + 7 + kMeshPreviewToolbarHeight);
}

[[nodiscard]] std::array<InspectorPropertyId, 4> MeshPreviewToolbarProperties() noexcept {
    return {
        InspectorPropertyId::MeshPreviewReset,
        InspectorPropertyId::MeshPreviewFit,
        InspectorPropertyId::MeshPreviewRenderMode,
        InspectorPropertyId::MeshPreviewLightPreset,
    };
}

[[nodiscard]] RECT MeshPreviewToolbarButtonRect(const RECT& toolbar, int index) noexcept {
    const int top = toolbar.top + (static_cast<int>(toolbar.bottom - toolbar.top) - kMeshPreviewToolbarButtonSize) / 2;
    const int left = toolbar.left + 6 + index * (kMeshPreviewToolbarButtonSize + kMeshPreviewToolbarButtonGap);
    return Rect(left, top, left + kMeshPreviewToolbarButtonSize, top + kMeshPreviewToolbarButtonSize);
}

[[nodiscard]] int MeshPreviewPanelHeight(const RECT& content) noexcept {
    const int panelWidth = std::max(1, static_cast<int>(content.right - content.left) - 24);
    return std::clamp(panelWidth * 62 / 100, kAssetPreviewMinHeight, kAssetPreviewMaxHeight);
}

[[nodiscard]] RECT MeshPreviewPanelRect(const RECT& content, int y) noexcept {
    return Rect(content.left + 12, y, content.right - 12, y + MeshPreviewPanelHeight(content));
}

void DrawPreviewResetIcon(HDC dc, RECT rect, COLORREF color) {
    ScopedPen pen(2, color);
    const ScopedGdiObject selectedPen(dc, pen.handle);
    Arc(dc, rect.left + 5, rect.top + 5, rect.right - 4, rect.bottom - 4, rect.left + 6, rect.top + 9, rect.left + 10, rect.top + 5);
    MoveToEx(dc, rect.left + 7, rect.top + 6, nullptr);
    LineTo(dc, rect.left + 5, rect.top + 11);
    LineTo(dc, rect.left + 11, rect.top + 10);
}

void DrawPreviewFitIcon(HDC dc, RECT rect, COLORREF color) {
    ScopedPen pen(2, color);
    const ScopedGdiObject selectedPen(dc, pen.handle);
    const int l = rect.left + 6;
    const int t = rect.top + 6;
    const int r = rect.right - 6;
    const int b = rect.bottom - 6;
    MoveToEx(dc, l, t + 5, nullptr);
    LineTo(dc, l, t);
    LineTo(dc, l + 5, t);
    MoveToEx(dc, r - 5, t, nullptr);
    LineTo(dc, r, t);
    LineTo(dc, r, t + 5);
    MoveToEx(dc, l, b - 5, nullptr);
    LineTo(dc, l, b);
    LineTo(dc, l + 5, b);
    MoveToEx(dc, r - 5, b, nullptr);
    LineTo(dc, r, b);
    LineTo(dc, r, b - 5);
}

void DrawPreviewModeIcon(HDC dc, RECT rect, COLORREF color, EditorMeshPreviewRenderMode mode) {
    ScopedPen pen(2, color);
    const ScopedGdiObject selectedPen(dc, pen.handle);
    const int l = rect.left + 6;
    const int t = rect.top + 7;
    const int r = rect.right - 6;
    const int b = rect.bottom - 6;
    MoveToEx(dc, l, t, nullptr);
    LineTo(dc, r, t);
    LineTo(dc, r, b);
    LineTo(dc, l, b);
    LineTo(dc, l, t);
    if (mode == EditorMeshPreviewRenderMode::WireframeOnly || mode == EditorMeshPreviewRenderMode::WireframeOverlay) {
        MoveToEx(dc, l, t, nullptr);
        LineTo(dc, r, b);
        MoveToEx(dc, r, t, nullptr);
        LineTo(dc, l, b);
    } else if (mode == EditorMeshPreviewRenderMode::Normals) {
        MoveToEx(dc, l + 3, b - 3, nullptr);
        LineTo(dc, l + 3, t + 3);
        MoveToEx(dc, l + 3, b - 3, nullptr);
        LineTo(dc, r - 2, b - 3);
        MoveToEx(dc, l + 3, b - 3, nullptr);
        LineTo(dc, r - 2, t + 2);
    } else if (mode == EditorMeshPreviewRenderMode::Bounds) {
        Ellipse(dc, l + 1, t + 1, r - 1, b - 1);
    } else {
        ScopedBrush brush(color);
        const ScopedGdiObject selectedBrush(dc, brush.handle);
        Rectangle(dc, l + 3, t + 3, r - 2, b - 2);
    }
}

void DrawPreviewLightIcon(HDC dc, RECT rect, COLORREF color, EditorMeshPreviewLightPreset preset) {
    ScopedPen pen(2, color);
    const ScopedGdiObject selectedPen(dc, pen.handle);
    ScopedBrush brush(color);
    const ScopedGdiObject selectedBrush(dc, brush.handle);
    const int cx = (rect.left + rect.right) / 2;
    const int cy = (rect.top + rect.bottom) / 2;
    const int offset = preset == EditorMeshPreviewLightPreset::Front ? 0 : preset == EditorMeshPreviewLightPreset::Rim ? 3 : -3;
    Ellipse(dc, cx - 3 + offset, cy - 3, cx + 4 + offset, cy + 4);
    MoveToEx(dc, rect.left + 6, rect.bottom - 6, nullptr);
    LineTo(dc, rect.right - 6, rect.bottom - 6);
}

void DrawPreviewToolbarIcon(HDC dc, RECT rect, COLORREF color, InspectorPropertyId property, const InspectorPanelState& inspector) {
    switch (property) {
    case InspectorPropertyId::MeshPreviewReset:
        DrawPreviewResetIcon(dc, rect, color);
        break;
    case InspectorPropertyId::MeshPreviewFit:
        DrawPreviewFitIcon(dc, rect, color);
        break;
    case InspectorPropertyId::MeshPreviewRenderMode:
        DrawPreviewModeIcon(dc, rect, color, inspector.MeshPreviewRenderMode());
        break;
    case InspectorPropertyId::MeshPreviewLightPreset:
        DrawPreviewLightIcon(dc, rect, color, inspector.MeshPreviewLightPreset());
        break;
    default:
        break;
    }
}

void DrawMeshPreviewToolbar(HDC dc, RECT toolbar, const EditorTheme& theme, const InspectorPanelState& inspector) {
    GdiDrawing::FillRectColor(dc, toolbar, Rgb(22, 25, 30));
    DrawFrame(dc, toolbar, Rgb(22, 25, 30), Rgb(36, 41, 48));

    const std::array<InspectorPropertyId, 4> properties = MeshPreviewToolbarProperties();
    for (int index = 0; index < static_cast<int>(properties.size()); ++index) {
        const InspectorPropertyId property = properties[static_cast<std::size_t>(index)];
        const RECT button = MeshPreviewToolbarButtonRect(toolbar, index);
        const bool hovered = inspector.IsHovered(InspectorHitKind::MeshPreviewToolbarButton, InspectorSectionId::Asset, property);
        const bool active = (property == InspectorPropertyId::MeshPreviewFit && inspector.MeshPreviewZoom() >= kMeshPreviewFitZoom - 0.01F)
            || (property == InspectorPropertyId::MeshPreviewRenderMode && inspector.MeshPreviewRenderMode() != EditorMeshPreviewRenderMode::Solid)
            || (property == InspectorPropertyId::MeshPreviewLightPreset && inspector.MeshPreviewLightPreset() != EditorMeshPreviewLightPreset::Studio);
        DrawFrame(dc, button, hovered || active ? HoverFill(theme) : Rgb(29, 33, 39), active ? Color(theme.accent) : Color(theme.borderPanel));
        DrawPreviewToolbarIcon(dc, button, active ? Color(theme.accent) : Color(theme.textPrimary), property, inspector);
    }
}

[[nodiscard]] int DrawMeshPreview(
    HDC dc,
    RECT content,
    int y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const kb::assets::AssetMetadata& metadata,
    bool deferPreviewWork) {
    EditorMeshPreviewService& previews = EditorMeshPreviewCache();
    const EditorMeshPreviewSettings previewSettings = MeshPreviewSettingsFromState(inspector);
    const EditorMeshThumbnailStats* stats = deferPreviewWork ? previews.CachedStatsFor(metadata) : previews.StatsFor(metadata);
    const EditorMeshThumbnailImage* image = deferPreviewWork ? previews.CachedPreviewFor(metadata, previewSettings) : previews.PreviewFor(metadata, previewSettings);
    if (image == nullptr && stats == nullptr) {
        const bool meshLikeAsset = metadata.type == "RenderMesh" || metadata.importCategory == "Mesh";
        if (!deferPreviewWork || !meshLikeAsset) {
            return y;
        }
    }

    RECT panel = MeshPreviewPanelRect(content, y);
    DrawFrame(dc, panel, Rgb(18, 21, 25), Color(theme.borderPanel));

    const RECT toolbar = MeshPreviewToolbarRect(panel);
    DrawMeshPreviewToolbar(dc, toolbar, theme, inspector);

    RECT preview = Shrink(panel, 12, 12 + kMeshPreviewToolbarHeight + 8, 12, 34);
    const int previewWidth = static_cast<int>(preview.right - preview.left);
    const int previewHeight = static_cast<int>(preview.bottom - preview.top);
    const int imageSize = std::max(32, std::min(previewWidth, previewHeight));
    RECT imageRect = Rect(
        preview.left + (previewWidth - imageSize) / 2,
        preview.top + (previewHeight - imageSize) / 2,
        preview.left + (previewWidth - imageSize) / 2 + imageSize,
        preview.top + (previewHeight - imageSize) / 2 + imageSize);
    if (image != nullptr) {
        DrawThumbnailBitmap(dc, imageRect, *image);
    } else {
        HeroIconPainter::Draw(dc, Shrink(imageRect, imageSize / 4, imageSize / 4, imageSize / 4, imageSize / 4), HeroIconKind::Cube, Color(theme.textSecondary), 2);
    }

    RECT footer = Rect(panel.left + 12, panel.bottom - 28, panel.right - 12, panel.bottom - 7);
    const std::string footerText = stats == nullptr
        ? std::string{ "Mesh preview" }
        : ("Mesh preview  |  " + FormatUInt64(stats->triangleCount) + " tris  |  " + FormatUInt64(stats->vertexCount) + " verts");
    ScopedFont font(11, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, font.handle);
    Text(dc, footer, footerText, Color(theme.textSecondary), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    return panel.bottom + kSectionGap;
}

[[nodiscard]] std::string FormatValidationIssue(const EditorMeshValidationIssue& issue) {
    switch (issue.severity) {
    case EditorMeshValidationSeverity::Error:
        return "Error: " + issue.message;
    case EditorMeshValidationSeverity::Warning:
        return "Warning: " + issue.message;
    case EditorMeshValidationSeverity::Info:
    default:
        return issue.message;
    }
}

void DrawHeader(HDC dc, RECT content, const EditorTheme& theme, HeroIconKind icon, std::string_view title, std::string_view subtitle) {
    RECT header = Rect(content.left, content.top, content.right, content.top + kHeaderHeight);
    GdiDrawing::FillRectColor(dc, header, Color(theme.panel));

    RECT iconCell = Rect(header.left + kHeaderPad, header.top + kHeaderPad, header.left + kHeaderPad + kHeaderIcon, header.top + kHeaderPad + kHeaderIcon);
    HeroIconPainter::Draw(dc, Shrink(iconCell, 4, 4, 4, 4), icon, Color(theme.textPrimary), 2);

    RECT titleRect = Rect(iconCell.right + 10, header.top + 8, header.right - 12, header.top + 32);
    RECT subtitleRect = Rect(iconCell.right + 10, titleRect.bottom, header.right - 12, header.bottom - 8);
    ScopedFont titleFont(14, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedFont(dc, titleFont.handle);
        Text(dc, titleRect, title, Color(theme.textPrimary));
    }
    Text(dc, subtitleRect, subtitle, Color(theme.textDisabled));
}

void DrawEmpty(HDC dc, RECT content, const EditorTheme& theme) {
    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));
    ScopedFont font(12, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, font.handle);
    Text(dc, content, "Select an object or asset to inspect it", Color(theme.textDisabled), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void PaintAsset(HDC dc, RECT content, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const bool deferMeshPreviewWork = sceneContext.HasActiveViewportCameraNavigation();
    int y = content.top;

    if (state.SelectionKind() == EditorAssetBrowserSelectionKind::Asset) {
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(state.SelectedAsset());
        if (metadata == nullptr) {
            DrawEmpty(dc, content, theme);
            return;
        }

        DrawHeader(dc, content, theme, HeroIconKind::Cube, metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name, metadata->type.empty() ? "Asset" : metadata->type);
        y += kHeaderHeight + kPanelPadTop;
        y = DrawMeshPreview(dc, content, y, theme, inspector, *metadata, deferMeshPreviewWork);
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Asset, HeroIconKind::Cube, "Asset");
        if (!metadata->importCategory.empty()) {
            section.Field("Category", metadata->importCategory);
        }
        if (!deferMeshPreviewWork) {
            if (const EditorMeshThumbnailStats* stats = EditorMeshPreviewCache().StatsFor(*metadata)) {
                section.Field("Vertices", FormatUInt64(stats->vertexCount));
                section.Field("Indices", FormatUInt64(stats->indexCount));
                section.Field("Triangles", FormatUInt64(stats->triangleCount));
                section.Field("Material Slots", FormatUInt64(stats->materialSlotCount));
                section.Field("Bounds Center", FormatVec3(stats->boundsCenter));
                section.Field("Bounds Radius", FormatFloat(stats->boundsRadius, 3));
            }
            if (const EditorMeshValidationResult* validation = EditorMeshPreviewCache().ValidationFor(*metadata)) {
                const std::size_t shown = std::min<std::size_t>(validation->issues.size(), 6U);
                for (std::size_t index = 0; index < shown; ++index) {
                    section.Field(index == 0U ? "Validation" : "", FormatValidationIssue(validation->issues[index]));
                }
            }
        }
        section.Field("Id", FormatUInt64(metadata->id.value));
        section.Field("Virtual Path", NormalizePath(metadata->virtualPath));
        if (const std::optional<std::filesystem::path> physical = ResolveAssetPhysicalPath(manager, *metadata)) {
            section.Field("Physical Path", physical->string());
        }
        section.Field("Content Hash", FormatUInt64(metadata->contentHash));
        section.Bool("Runtime Loadable", metadata->runtimeLoadable);
        section.Bool("Loaded", manager.IsLoaded(metadata->id));
        return;
    }

}

void PaintEntity(HDC dc, RECT content, const EditorTheme& theme, const EditorSceneContext& sceneContext, kb::scene::SceneEntity selected) {
    const kb::scene::Scene& scene = sceneContext.Scene();
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const std::vector<kb::scene::SceneEntity>& selectedEntities = sceneContext.SelectedHierarchyEntities();
    const bool multi = selectedEntities.size() > 1U;
    const std::string title = multi ? ("-- (" + std::to_string(selectedEntities.size()) + ")") : scene.Entities().Name(selected);
    const std::string subtitle = "Entity " + FormatUInt64(selected.Id());

    DrawHeader(dc, content, theme, HeroIconKind::Cube, title, subtitle);
    int y = content.top + kHeaderHeight + kPanelPadTop;

    const kb::scene::VisibilityComponent visibility = scene.Components().Visibility().Get(selected);
    {
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::General, HeroIconKind::AdjustmentsHorizontal, "General");
        section.Field("Name", scene.Entities().Name(selected), InspectorPropertyId::EntityName);
        section.Bool("Visible", visibility.visible, InspectorPropertyId::EntityVisible);
        y = section.Bottom() + kSectionGap;
    }

    const kb::scene::TransformComponent transform = scene.Transforms().Get(selected);
    {
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Transform, HeroIconKind::Gamepad2, "Transform");
        section.Vec3("Position", transform.localPosition, InspectorPropertyId::PositionX, InspectorPropertyId::PositionY, InspectorPropertyId::PositionZ);
        section.Rotation("Rotation", transform.localRotation);
        section.Vec3("Scale", transform.localScale, InspectorPropertyId::ScaleX, InspectorPropertyId::ScaleY, InspectorPropertyId::ScaleZ);
        y = section.Bottom() + kSectionGap;
    }
}

[[nodiscard]] InspectorPanelRenderer::Hit MakeHit(InspectorHitKind kind, InspectorSectionId section, InspectorPropertyId property, RECT rect) noexcept {
    return InspectorPanelRenderer::Hit{
        .kind = kind,
        .section = section,
        .property = property,
        .rect = rect,
    };
}

[[nodiscard]] RECT RowRect(RECT bounds, int y) noexcept {
    return Rect(bounds.left, y, bounds.right, y + kFieldRowHeight);
}

[[nodiscard]] RECT ValueRectForRow(RECT row) noexcept {
    RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    const int top = CenteredY(row, kValueHeight);
    return Rect(labelRect.right, top, row.right - kRowPadX, top + kValueHeight);
}

[[nodiscard]] InspectorPanelRenderer::Hit HitBool(RECT row, InspectorSectionId section, InspectorPropertyId property, int x, int y) noexcept {
    RECT box = CheckboxRectForRow(row);
    if (Contains(box, x, y)) {
        return MakeHit(InspectorHitKind::BoolField, section, property, box);
    }
    return Contains(row, x, y) ? MakeHit(InspectorHitKind::Row, section, property, row) : InspectorPanelRenderer::Hit{};
}

[[nodiscard]] InspectorPanelRenderer::Hit HitFloatRow(RECT row, InspectorSectionId section, InspectorPropertyId property, int x, int y) noexcept {
    RECT value = ValueRectForRow(row);
    if (Contains(value, x, y)) {
        return MakeHit(InspectorHitKind::FloatField, section, property, value);
    }
    return Contains(row, x, y) ? MakeHit(InspectorHitKind::Row, section, property, row) : InspectorPanelRenderer::Hit{};
}

[[nodiscard]] InspectorPanelRenderer::Hit HitTextRow(RECT row, InspectorSectionId section, InspectorPropertyId property, int x, int y) noexcept {
    RECT value = ValueRectForRow(row);
    if (Contains(value, x, y)) {
        return MakeHit(InspectorHitKind::TextField, section, property, value);
    }
    return Contains(row, x, y) ? MakeHit(InspectorHitKind::Row, section, property, row) : InspectorPanelRenderer::Hit{};
}

[[nodiscard]] InspectorPanelRenderer::Hit HitVec3(RECT row, InspectorSectionId section, InspectorPropertyId px, InspectorPropertyId py, InspectorPropertyId pz, int x, int y) noexcept {
    RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    RECT valueRect = Rect(labelRect.right, row.top, row.right - kRowPadX, row.bottom);
    const int valueWidth = static_cast<int>(valueRect.right - valueRect.left);
    const int available = std::max(0, valueWidth - (kLaneGap * 2));
    const int laneWidth = std::max<int>(44, available / 3);
    const int lanesWidth = laneWidth * 3 + kLaneGap * 2;
    const int laneLeft = static_cast<int>(valueRect.left) + std::max(0, (valueWidth - lanesWidth) / 2);
    const InspectorPropertyId properties[3]{ px, py, pz };
    for (int i = 0; i < 3; ++i) {
        RECT lane = Rect(laneLeft + (laneWidth + kLaneGap) * i, valueRect.top, laneLeft + (laneWidth + kLaneGap) * i + laneWidth, valueRect.bottom);
        const int top = CenteredY(lane, kValueHeight);
        RECT box = Rect(lane.left + kAxisLetterWidth + kAxisGap, top, lane.right, top + kValueHeight);
        if (Contains(box, x, y)) {
            return MakeHit(InspectorHitKind::FloatField, section, properties[i], box);
        }
    }
    if (Contains(row, x, y)) {
        return MakeHit(InspectorHitKind::Row, section, px, row);
    }
    return {};
}

[[nodiscard]] InspectorPanelRenderer::Hit HitRotation(RECT row, int x, int y) noexcept {
    RECT labelRect = Rect(row.left + kRowPadX, row.top, row.left + ((row.right - row.left) * 36 / 100), row.bottom);
    RECT valueRect = Rect(labelRect.right, row.top, row.right - kRowPadX, row.bottom);
    const int valueWidth = static_cast<int>(valueRect.right - valueRect.left);
    const int available = std::max(0, valueWidth - (kLaneGap * 2));
    const int laneWidth = std::max<int>(44, available / 3);
    const int lanesWidth = laneWidth * 3 + kLaneGap * 2;
    const int laneLeft = static_cast<int>(valueRect.left) + std::max(0, (valueWidth - lanesWidth) / 2);
    const InspectorPropertyId properties[3]{
        InspectorPropertyId::RotationX,
        InspectorPropertyId::RotationY,
        InspectorPropertyId::RotationZ,
    };
    for (int i = 0; i < 3; ++i) {
        RECT lane = Rect(laneLeft + (laneWidth + kLaneGap) * i, valueRect.top, laneLeft + (laneWidth + kLaneGap) * i + laneWidth, valueRect.bottom);
        const int top = CenteredY(lane, kValueHeight);
        RECT box = Rect(lane.left + kAxisLetterWidth + kAxisGap, top, lane.right, top + kValueHeight);
        if (Contains(box, x, y)) {
            return MakeHit(InspectorHitKind::FloatField, InspectorSectionId::Transform, properties[i], box);
        }
    }
    if (Contains(row, x, y)) {
        return MakeHit(InspectorHitKind::Row, InspectorSectionId::Transform, InspectorPropertyId::RotationX, row);
    }
    return {};
}

[[nodiscard]] InspectorPanelRenderer::Hit HitSectionHeader(RECT bounds, int& y, const InspectorPanelState& state, InspectorSectionId section, int x, int yPoint) noexcept {
    RECT header = Rect(bounds.left, y, bounds.right, y + kSectionHeaderHeight);
    y += kSectionHeaderHeight;
    if (Contains(header, x, yPoint)) {
        return MakeHit(InspectorHitKind::SectionHeader, section, InspectorPropertyId::None, header);
    }
    if (!state.IsCollapsed(section)) {
        y += kDividerHeight;
    }
    return {};
}

void AdvanceRow(int& y) noexcept {
    y += kFieldRowHeight + kDividerHeight;
}

} // namespace

void InspectorPanelRenderer::Paint(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext) const {
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, content.left, content.top, content.right, content.bottom);

    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));
    const RECT inner = Rect(content.left, content.top, content.right, content.bottom);

    if (sceneContext.AssetBrowser().SelectionKind() == EditorAssetBrowserSelectionKind::Asset) {
        PaintAsset(dc, inner, theme, sceneContext);
        RestoreDC(dc, savedDc);
        return;
    }

    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        DrawEmpty(dc, inner, theme);
        RestoreDC(dc, savedDc);
        return;
    }

    PaintEntity(dc, inner, theme, sceneContext, selected);
    RestoreDC(dc, savedDc);
}

InspectorPanelRenderer::Hit InspectorPanelRenderer::HitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int yPoint) noexcept {
    if (!Contains(content, x, yPoint)) {
        return {};
    }

    const InspectorPanelState& state = sceneContext.Inspector();
    int y = content.top + kHeaderHeight + kPanelPadTop;

    if (sceneContext.AssetBrowser().SelectionKind() == EditorAssetBrowserSelectionKind::Asset) {
        const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().Find(sceneContext.AssetBrowser().SelectedAsset())) {
            if (EditorMeshPreviewCache().StatsFor(*metadata) != nullptr) {
                const RECT preview = MeshPreviewPanelRect(content, y);
                const RECT toolbar = MeshPreviewToolbarRect(preview);
                const std::array<InspectorPropertyId, 4> properties = MeshPreviewToolbarProperties();
                for (int index = 0; index < static_cast<int>(properties.size()); ++index) {
                    const RECT button = MeshPreviewToolbarButtonRect(toolbar, index);
                    if (Contains(button, x, yPoint)) {
                        return MakeHit(InspectorHitKind::MeshPreviewToolbarButton, InspectorSectionId::Asset, properties[static_cast<std::size_t>(index)], button);
                    }
                }
                if (Contains(preview, x, yPoint)) {
                    return MakeHit(InspectorHitKind::MeshPreview, InspectorSectionId::Asset, InspectorPropertyId::None, preview);
                }
                y += MeshPreviewPanelHeight(content) + kSectionGap;
            }
        }
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(content, y, state, InspectorSectionId::Asset, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        return {};
    }

    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        return {};
    }

    if (InspectorPanelRenderer::Hit hit = HitSectionHeader(content, y, state, InspectorSectionId::General, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (!state.IsCollapsed(InspectorSectionId::General)) {
        if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(content, y), InspectorSectionId::General, InspectorPropertyId::EntityName, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
        if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(content, y), InspectorSectionId::General, InspectorPropertyId::EntityVisible, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
    }
    y += kSectionGap;

    if (InspectorPanelRenderer::Hit hit = HitSectionHeader(content, y, state, InspectorSectionId::Transform, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (!state.IsCollapsed(InspectorSectionId::Transform)) {
        if (InspectorPanelRenderer::Hit hit = HitVec3(RowRect(content, y), InspectorSectionId::Transform, InspectorPropertyId::PositionX, InspectorPropertyId::PositionY, InspectorPropertyId::PositionZ, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
        if (InspectorPanelRenderer::Hit hit = HitRotation(RowRect(content, y), x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
        if (InspectorPanelRenderer::Hit hit = HitVec3(RowRect(content, y), InspectorSectionId::Transform, InspectorPropertyId::ScaleX, InspectorPropertyId::ScaleY, InspectorPropertyId::ScaleZ, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
    }
    y += kSectionGap;

    return {};
}

} // namespace kb::editor

#endif
