#include "rendering/InspectorPanelRenderer.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorComponentLabelFormatter.hpp"
#include "inspection/InspectorMaterialTextureSlotFormatter.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "rendering/EditorMeshPreviewService.hpp"
#include "rendering/EditorTexturePreviewService.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/GdiResources.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/gdi/ScopedBrush.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/gdi/ScopedPen.hpp"
#include "scene/EditorSceneSelectionPivot.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
constexpr int kComponentMenuButtonSize = 18;
constexpr int kAddComponentButtonHeight = 24;
constexpr int kAddComponentBrowserMaxHeight = 280;
constexpr int kAddComponentSearchHeight = 24;
constexpr int kAddComponentResultRowHeight = 26;
constexpr int kAddComponentCategoryHeaderHeight = 22;
constexpr int kScrollbarWidth = 12;
constexpr int kScrollbarInset = 3;
constexpr int kScrollbarMinThumb = 28;
constexpr float kMeshPreviewFitZoom = 1.35F;

enum class InspectorRowValueKind : std::uint8_t {
    Text,
    Bool,
};

struct InspectorRowDefinition {
    InspectorPropertyId property = InspectorPropertyId::None;
    InspectorRowValueKind kind = InspectorRowValueKind::Text;
};

constexpr std::array<InspectorRowDefinition, 10> kAudioSourceRows{ {
    { InspectorPropertyId::AudioSourceClip, InspectorRowValueKind::Text },
    { InspectorPropertyId::AudioSourceVolume, InspectorRowValueKind::Text },
    { InspectorPropertyId::AudioSourcePitch, InspectorRowValueKind::Text },
    { InspectorPropertyId::AudioSourceEnabled, InspectorRowValueKind::Bool },
    { InspectorPropertyId::AudioSourceAutoplay, InspectorRowValueKind::Bool },
    { InspectorPropertyId::AudioSourceLoop, InspectorRowValueKind::Bool },
    { InspectorPropertyId::AudioSourceMute, InspectorRowValueKind::Bool },
    { InspectorPropertyId::AudioSourceSpatial, InspectorRowValueKind::Bool },
    { InspectorPropertyId::AudioSourceAttenuation, InspectorRowValueKind::Text },
    { InspectorPropertyId::AudioSourceRange, InspectorRowValueKind::Text },
} };

constexpr std::array<InspectorRowDefinition, 2> kAudioListenerRows{ {
    { InspectorPropertyId::AudioListenerEnabled, InspectorRowValueKind::Bool },
    { InspectorPropertyId::AudioListenerPrimary, InspectorRowValueKind::Bool },
} };

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

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

[[nodiscard]] int RectWidth(const RECT& rect) noexcept {
    return std::max(0L, rect.right - rect.left);
}

[[nodiscard]] RECT ContentViewportRect(RECT content, bool reserveScrollbar) noexcept {
    if (reserveScrollbar) {
        content.right -= kScrollbarWidth + 4;
    }
    return content;
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

[[nodiscard]] RECT ComponentRemoveButtonRect(RECT header) noexcept {
    const int top = CenteredY(header, kComponentMenuButtonSize);
    return Rect(header.right - kRowPadX - kComponentMenuButtonSize, top, header.right - kRowPadX, top + kComponentMenuButtonSize);
}

void DrawSectionHeader(HDC dc, RECT rect, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, HeroIconKind icon, std::string_view title, bool removeButton = false) {
    const bool hovered = state.IsHovered(InspectorHitKind::SectionHeader, section, InspectorPropertyId::None);
    GdiDrawing::FillRectColor(dc, rect, hovered ? HoverFill(theme) : Color(theme.strip));

    RECT chevron = Rect(rect.left + 9, rect.top, rect.left + 29, rect.bottom);
    DrawTriangle(dc, Shrink(chevron, 4, 4, 4, 4), !state.IsCollapsed(section), Color(theme.textSecondary));

    RECT iconRect = Rect(rect.left + 35, rect.top + 3, rect.left + 53, rect.top + 21);
    HeroIconPainter::Draw(dc, iconRect, icon, Color(theme.textSecondary), 2);

    RECT titleRect = Rect(rect.left + 59, rect.top, removeButton ? rect.right - 44 : rect.right - 8, rect.bottom);
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
    SectionWriter(HDC dc, RECT bounds, const EditorTheme& theme, const InspectorPanelState& state, InspectorSectionId section, HeroIconKind icon, std::string_view title, bool menuButton = false)
        : dc_(dc), bounds_(bounds), theme_(theme), state_(state), section_(section), collapsed_(state.IsCollapsed(section)) {
        DrawSectionHeader(dc_, Rect(bounds_.left, y_, bounds_.right, y_ + kSectionHeaderHeight), theme_, state_, section_, icon, title, menuButton);
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

    void Float(std::string_view label, std::string_view value, InspectorPropertyId property) {
        Field(label, value, property);
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

[[nodiscard]] RECT AddComponentButtonRect(RECT content, int y) noexcept {
    const int width = std::min<int>(240, std::max<int>(120, static_cast<int>(content.right - content.left) - 32));
    const int left = content.left + std::max(16, (static_cast<int>(content.right - content.left) - width) / 2);
    return Rect(left, y, left + width, y + kAddComponentButtonHeight);
}

[[nodiscard]] RECT AddComponentBrowserRect(RECT content, int y) noexcept {
    const RECT button = AddComponentButtonRect(content, y);
    const int top = button.bottom + 8;
    const int left = content.left + 12;
    const int right = content.right - 12;
    const int bottom = std::min(static_cast<int>(content.bottom), top + kAddComponentBrowserMaxHeight);
    return Rect(left, top, right, bottom);
}

[[nodiscard]] RECT AddComponentSearchRect(RECT browser) noexcept {
    return Rect(browser.left + 10, browser.top + 34, browser.right - 10, browser.top + 34 + kAddComponentSearchHeight);
}

[[nodiscard]] RECT AddComponentResultsRect(RECT browser) noexcept {
    return Rect(browser.left + 1, browser.top + 68, browser.right - 1, browser.bottom - 1);
}

[[nodiscard]] std::string_view AddComponentQuery(const InspectorPanelState& inspector) noexcept {
    return inspector.EditedProperty() == InspectorPropertyId::AddComponentSearch ? std::string_view{ inspector.EditBuffer() } : std::string_view{};
}

void DrawAddComponentBrowser(HDC dc, RECT content, const EditorTheme& theme, const InspectorPanelState& inspector, int y) {
    const RECT browser = AddComponentBrowserRect(content, y);
    DrawFrame(dc, browser, Rgb(30, 33, 38), Rgb(70, 78, 88));

    ScopedFont titleFont(12, FW_SEMIBOLD);
    {
        const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
        Text(dc, Rect(browser.left + 10, browser.top + 4, browser.right - 10, browser.top + 28), "Add Component", Color(theme.textPrimary));
    }

    const RECT search = AddComponentSearchRect(browser);
    const bool searchFocused = inspector.EditedProperty() == InspectorPropertyId::AddComponentSearch;
    DrawFrame(dc, search, Rgb(20, 22, 25), searchFocused ? Color(theme.accent) : Rgb(54, 60, 68));
    const std::string_view query = AddComponentQuery(inspector);
    if (!query.empty()) {
        Text(dc, Shrink(search, 8, 0, 8, 0), query, Color(theme.textPrimary));
    }

    const std::vector<const InspectorComponentTile*> tiles = InspectorComponentCatalog::Search(query);
    const RECT results = AddComponentResultsRect(browser);
    int rowTop = results.top;
    std::string category;
    for (const InspectorComponentTile* tile : tiles) {
        if (tile == nullptr) {
            continue;
        }
        if (tile->category != category) {
            category = tile->category;
            const RECT categoryRow = Rect(results.left, rowTop, results.right, rowTop + kAddComponentCategoryHeaderHeight);
            if (categoryRow.bottom > results.bottom) {
                break;
            }
            GdiDrawing::FillRectColor(dc, categoryRow, Rgb(25, 28, 33));
            ScopedFont categoryFont(11, FW_SEMIBOLD);
            const ScopedGdiObject selectedCategoryFont(dc, categoryFont.handle);
            Text(dc, Rect(categoryRow.left + 10, categoryRow.top, categoryRow.right - 10, categoryRow.bottom), category, Color(theme.textSecondary));
            rowTop += kAddComponentCategoryHeaderHeight;
        }
        const RECT row = Rect(results.left, rowTop, results.right, rowTop + kAddComponentResultRowHeight);
        if (row.bottom > results.bottom) {
            break;
        }
        Text(dc, Rect(row.left + 22, row.top, row.right - 10, row.bottom), tile->label, Color(theme.textPrimary));
        rowTop += kAddComponentResultRowHeight;
    }

    if (tiles.empty()) {
        Text(dc, results, "No components found", Rgb(122, 130, 144), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawAddComponent(HDC dc, RECT content, const EditorTheme& theme, const InspectorPanelState& inspector, int y) {
    const RECT button = AddComponentButtonRect(content, y);
    DrawFrame(dc, button, inspector.IsAddComponentBrowserOpen() ? HoverFill(theme) : Color(theme.chrome), Color(theme.borderPanel));
    ScopedFont font(12, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, font.handle);
    Text(dc, button, "Add Component", Color(theme.textPrimary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (inspector.IsAddComponentBrowserOpen()) {
        DrawAddComponentBrowser(dc, content, theme, inspector, y);
    }
}

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

[[nodiscard]] int TextureDetailsImageWidth(const RECT& content) noexcept {
    return std::max(48, static_cast<int>(content.right - content.left) - 24);
}

[[nodiscard]] int TextureDetailsImageHeight(const RECT& content, const EditorTexturePreviewImage& image) noexcept {
    const int width = TextureDetailsImageWidth(content);
    return std::max(48, (width * image.height) / std::max(1, image.width));
}

[[nodiscard]] RECT TextureDetailsPanelRect(const RECT& content, int y, const EditorTexturePreviewImage* image) noexcept {
    const int imageHeight = image == nullptr ? 92 : TextureDetailsImageHeight(content, *image);
    return Rect(content.left, y, content.right, y + kSectionHeaderHeight + kDividerHeight + imageHeight + 20);
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

[[nodiscard]] int DrawTextureDetails(
    HDC dc,
    RECT content,
    int y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const kb::assets::AssetMetadata& metadata) {
    if (!EditorTexturePreviewService::IsTextureAsset(metadata)) {
        return y;
    }

    const EditorTexturePreviewImage* image = EditorTexturePreviewService::PreviewFor(metadata);
    const RECT header = Rect(content.left, y, content.right, y + kSectionHeaderHeight);
    DrawSectionHeader(dc, header, theme, inspector, InspectorSectionId::Details, HeroIconKind::Eye, "Details");
    y += kSectionHeaderHeight;
    if (inspector.IsCollapsed(InspectorSectionId::Details)) {
        return y + kSectionGap;
    }

    DrawDivider(dc, content.left, content.right, y);
    y += kDividerHeight;
    const int imageWidth = TextureDetailsImageWidth(content);
    const int imageHeight = image == nullptr ? 92 : TextureDetailsImageHeight(content, *image);
    RECT frame = Rect(content.left + 12, y + 10, content.left + 12 + imageWidth, y + 10 + imageHeight);
    DrawFrame(dc, frame, Rgb(14, 16, 20), Color(theme.borderPanel));
    if (image != nullptr) {
        EditorTexturePreviewService::DrawContain(dc, frame, *image, false);
    } else {
        ScopedFont font(12, FW_NORMAL);
        const ScopedGdiObject selectedFont(dc, font.handle);
        Text(dc, frame, "Texture preview unavailable", Color(theme.textDisabled), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    return frame.bottom + 10 + kSectionGap;
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

// Resolves an asset id to a display label for inspector fields that reference
// another asset (mapping-context on a component, action on a mapping).
[[nodiscard]] std::string AssetDisplayName(const EditorSceneContext& sceneContext, std::uint64_t id) {
    if (id == 0U) {
        return "(none)";
    }
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    if (const kb::assets::AssetMetadata* metadata = manager.Registry().Find(kb::assets::AssetId{ id }); metadata != nullptr) {
        return metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
    }
    return "(missing)";
}

[[nodiscard]] std::string MaterialDisplayName(const EditorSceneContext& sceneContext, std::uint64_t id) {
    if (id == 0U) {
        return "None";
    }
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    if (const kb::assets::AssetMetadata* metadata = manager.Registry().Find(kb::assets::AssetId{ id }); metadata != nullptr) {
        return metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
    }
    return "Missing material asset " + std::to_string(id);
}

[[nodiscard]] std::optional<kb::render::RenderMeshAssetData> LoadMeshAssetData(const EditorSceneContext& sceneContext, std::uint64_t meshAssetId) {
    if (meshAssetId == 0U) {
        return std::nullopt;
    }
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(kb::assets::AssetId{ meshAssetId });
    if (metadata == nullptr) {
        return std::nullopt;
    }
    const std::optional<std::filesystem::path> resolved = ResolveAssetPhysicalPath(manager, *metadata);
    if (!resolved.has_value()) {
        return std::nullopt;
    }

    kb::render::RenderMeshAssetLoader loader;
    kb::assets::AssetLoadResult result = loader.Load(kb::assets::AssetLoadRequest{
        .metadata = *metadata,
        .resolvedPath = *resolved,
    });
    if (!result.Succeeded() || result.asset == nullptr) {
        return std::nullopt;
    }
    std::shared_ptr<kb::render::RenderMeshAssetData> mesh = std::static_pointer_cast<kb::render::RenderMeshAssetData>(result.asset);
    return mesh == nullptr ? std::nullopt : std::optional<kb::render::RenderMeshAssetData>{ *mesh };
}

[[nodiscard]] int MeshRendererMaterialSlotRows(const EditorSceneContext& sceneContext, const kb::scene::MeshRendererComponent& renderer) {
    std::uint32_t rows = std::max<std::uint32_t>(1U, renderer.materialSlotOverrideCount);
    if (const std::optional<kb::render::RenderMeshAssetData> mesh = LoadMeshAssetData(sceneContext, renderer.meshAssetId)) {
        rows = std::max<std::uint32_t>(rows, static_cast<std::uint32_t>(std::max(mesh->materialSlots.size(), mesh->materialNames.size())));
    }
    return static_cast<int>(std::min<std::uint32_t>(rows, kb::scene::kMaxMeshRendererMaterialSlotOverrides));
}

[[nodiscard]] std::string MeshRendererMaterialSlotLabel(const std::optional<kb::render::RenderMeshAssetData>& mesh, std::uint32_t slotIndex) {
    std::string label = "Slot " + std::to_string(slotIndex);
    if (mesh.has_value() && slotIndex < mesh->materialNames.size() && !mesh->materialNames[slotIndex].empty()) {
        label += " (" + mesh->materialNames[slotIndex] + ")";
    }
    return label;
}

[[nodiscard]] InspectorPropertyId MeshRendererMaterialSlotProperty(std::uint32_t slotIndex) noexcept {
    switch (slotIndex) {
    case 0U:
        return InspectorPropertyId::MeshRendererMaterialSlot0;
    case 1U:
        return InspectorPropertyId::MeshRendererMaterialSlot1;
    case 2U:
        return InspectorPropertyId::MeshRendererMaterialSlot2;
    case 3U:
        return InspectorPropertyId::MeshRendererMaterialSlot3;
    case 4U:
        return InspectorPropertyId::MeshRendererMaterialSlot4;
    case 5U:
        return InspectorPropertyId::MeshRendererMaterialSlot5;
    case 6U:
        return InspectorPropertyId::MeshRendererMaterialSlot6;
    case 7U:
        return InspectorPropertyId::MeshRendererMaterialSlot7;
    default:
        return InspectorPropertyId::None;
    }
}

[[nodiscard]] std::string AlphaModeName(kb::render::RenderMaterialAlphaMode mode) {
    switch (mode) {
    case kb::render::RenderMaterialAlphaMode::Opaque:
        return "Opaque";
    case kb::render::RenderMaterialAlphaMode::Mask:
        return "Mask";
    case kb::render::RenderMaterialAlphaMode::Blend:
        return "Blend";
    }
    return "Opaque";
}

// --- Inline Value Type dropdown (shared geometry for paint + hit-test) ---
constexpr int kValueTypeOptionCount = 4;
constexpr std::array<std::string_view, kValueTypeOptionCount> kValueTypeLabels{ "Bool", "Axis1D", "Axis2D", "Axis3D" };

[[nodiscard]] RECT ValueTypeRowRect(const RECT& content) noexcept {
    // The Input Action section is the first section; Value Type is its 2nd field.
    const int top = content.top + kHeaderHeight + kPanelPadTop + kSectionHeaderHeight
        + 2 * kDividerHeight + kFieldRowHeight;
    return Rect(content.left, top, content.right, top + kFieldRowHeight);
}

[[nodiscard]] RECT ValueTypeOptionRect(const RECT& content, int index) noexcept {
    const RECT row = ValueTypeRowRect(content);
    const int labelRight = row.left + ((row.right - row.left) * 36 / 100);
    const int top = row.bottom + index * kFieldRowHeight;
    return Rect(labelRight, top, row.right - kRowPadX, top + kFieldRowHeight);
}

void PaintValueTypeDropdown(HDC dc, const RECT& content, const EditorTheme& theme, const InspectorPanelState& inspector) {
    if (!inspector.IsValueTypeDropdownOpen()) {
        return;
    }
    for (int index = 0; index < kValueTypeOptionCount; ++index) {
        const RECT option = ValueTypeOptionRect(content, index);
        const bool hovered = inspector.ValueTypeDropdownHover() == index;
        GdiDrawing::FillRectColor(dc, option, hovered ? HoverFill(theme) : Color(theme.chrome));
        DrawFrame(dc, option, Color(theme.borderPanel), Color(theme.borderPanel));
        RECT text = Rect(option.left + 10, option.top, option.right - 6, option.bottom);
        ScopedFont labelFont(12, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        Text(dc, text, kValueTypeLabels[static_cast<std::size_t>(index)], Color(theme.textPrimary));
    }
}

void PaintInputActionAsset(HDC dc, RECT content, const EditorTheme& theme, const EditorSceneContext& sceneContext, const kb::assets::AssetMetadata& metadata) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const kb::input::InputActionAsset action = sceneContext.ReadInputActionAsset(metadata.id).value_or(kb::input::InputActionAsset{});

    DrawHeader(dc, content, theme, HeroIconKind::Gamepad2, action.name.empty() ? metadata.name : action.name, "Input Action");
    int y = content.top + kHeaderHeight + kPanelPadTop;
    {
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::InputAction, HeroIconKind::Gamepad2, "Input Action");
        section.Field("Name", action.name, InspectorPropertyId::InputActionName);
        section.Field("Value Type", kb::input::ToString(action.valueType), InspectorPropertyId::InputActionValueType);
        section.Bool("Consume Input", action.consumeInput, InspectorPropertyId::InputActionConsume);
        y = section.Bottom() + kSectionGap;
    }
    {
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Asset, HeroIconKind::Cube, "Asset");
        section.Field("Id", FormatUInt64(metadata.id.value));
        section.Field("Virtual Path", NormalizePath(metadata.virtualPath));
    }
    // Drawn last so the open dropdown overlays the rows beneath the Value Type field.
    PaintValueTypeDropdown(dc, content, theme, inspector);
}

void PaintInputMappingContextAsset(HDC dc, RECT content, const EditorTheme& theme, const EditorSceneContext& sceneContext, const kb::assets::AssetMetadata& metadata) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const kb::input::InputMappingContextAsset context = sceneContext.ReadInputMappingContextAsset(metadata.id).value_or(kb::input::InputMappingContextAsset{});

    DrawHeader(dc, content, theme, HeroIconKind::Gamepad2, metadata.name.empty() ? metadata.virtualPath.filename().string() : metadata.name, "Input Mapping Context");
    int y = content.top + kHeaderHeight + kPanelPadTop;

    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::InputMappings, HeroIconKind::Gamepad2, "Mappings");
    for (std::size_t index = 0; index < context.mappings.size(); ++index) {
        const kb::input::InputKeyMapping& mapping = context.mappings[index];
        const std::string suffix = " " + std::to_string(index);
        const bool capturing = inspector.IsListeningForKey() && inspector.KeyCaptureMappingIndex() == static_cast<int>(index);
        const std::string keyText = capturing ? std::string{ "Press a key..." } : std::string{ kb::input::ToString(mapping.key) };
        const std::string triggerText = mapping.triggers.empty()
            ? std::string{ "Down (implicit)" }
            : std::string{ kb::input::ToString(mapping.triggers.front().type) };
        section.Field("Key" + suffix, keyText, InspectorPropertyId::InputMappingKey);
        section.Field("Action" + suffix, AssetDisplayName(sceneContext, mapping.actionId), InspectorPropertyId::InputMappingAction);
        section.Field("Scale" + suffix, FormatFloat(mapping.scale), InspectorPropertyId::InputMappingScale);
        section.Field("Trigger" + suffix, triggerText, InspectorPropertyId::InputMappingTrigger);
        section.Field("", "Remove", InspectorPropertyId::InputMappingRemove);
    }
    section.Field("", "Add Mapping", InspectorPropertyId::InputMappingAdd);
}

void PaintMaterialAsset(HDC dc, RECT content, const EditorTheme& theme, const EditorSceneContext& sceneContext, const kb::assets::AssetMetadata& metadata) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const kb::render::RenderMaterialAssetData material = sceneContext.ReadMaterialAsset(metadata.id).value_or(kb::render::RenderMaterialAssetData{});

    DrawHeader(dc, content, theme, HeroIconKind::Cube, metadata.name.empty() ? metadata.virtualPath.filename().string() : metadata.name, "Render Material");
    int y = content.top + kHeaderHeight + kPanelPadTop;
    {
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Material, HeroIconKind::AdjustmentsHorizontal, "Material");
        section.Float("Base R", FormatFloat(material.desc.baseColor[0]), InspectorPropertyId::MaterialBaseColorR);
        section.Float("Base G", FormatFloat(material.desc.baseColor[1]), InspectorPropertyId::MaterialBaseColorG);
        section.Float("Base B", FormatFloat(material.desc.baseColor[2]), InspectorPropertyId::MaterialBaseColorB);
        section.Float("Base A", FormatFloat(material.desc.baseColor[3]), InspectorPropertyId::MaterialBaseColorA);
        section.Float("Metallic", FormatFloat(material.desc.metallicFactor), InspectorPropertyId::MaterialMetallicFactor);
        section.Float("Roughness", FormatFloat(material.desc.roughnessFactor), InspectorPropertyId::MaterialRoughnessFactor);
        section.Float("Normal Scale", FormatFloat(material.desc.normalScale), InspectorPropertyId::MaterialNormalScale);
        section.Float("Occlusion", FormatFloat(material.desc.occlusionStrength), InspectorPropertyId::MaterialOcclusionStrength);
        section.Float("Emissive R", FormatFloat(material.desc.emissiveColor[0]), InspectorPropertyId::MaterialEmissiveColorR);
        section.Float("Emissive G", FormatFloat(material.desc.emissiveColor[1]), InspectorPropertyId::MaterialEmissiveColorG);
        section.Float("Emissive B", FormatFloat(material.desc.emissiveColor[2]), InspectorPropertyId::MaterialEmissiveColorB);
        section.Float("Emissive Strength", FormatFloat(material.desc.emissiveStrength), InspectorPropertyId::MaterialEmissiveStrength);
        section.Float("Alpha Cutoff", FormatFloat(material.desc.alphaCutoff), InspectorPropertyId::MaterialAlphaCutoff);
        section.Field("Alpha Mode", AlphaModeName(material.desc.alphaMode), InspectorPropertyId::MaterialAlphaMode);
        section.Bool("Double Sided", material.desc.doubleSided, InspectorPropertyId::MaterialDoubleSided);
        const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
        section.Field("Albedo", InspectorMaterialTextureSlotFormatter::DisplayName(manager, material.desc.albedoTextureAssetId), InspectorPropertyId::MaterialAlbedoTexture);
        section.Field("Normal", InspectorMaterialTextureSlotFormatter::DisplayName(manager, material.desc.normalTextureAssetId), InspectorPropertyId::MaterialNormalTexture);
        section.Field("Metallic-Roughness", InspectorMaterialTextureSlotFormatter::DisplayName(manager, material.desc.metallicRoughnessTextureAssetId), InspectorPropertyId::MaterialMetallicRoughnessTexture);
        section.Field("Occlusion", InspectorMaterialTextureSlotFormatter::DisplayName(manager, material.desc.occlusionTextureAssetId), InspectorPropertyId::MaterialOcclusionTexture);
        section.Field("Emissive", InspectorMaterialTextureSlotFormatter::DisplayName(manager, material.desc.emissiveTextureAssetId), InspectorPropertyId::MaterialEmissiveTexture);
        y = section.Bottom() + kSectionGap;
    }
    {
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Asset, HeroIconKind::Cube, "Asset");
        section.Field("Id", FormatUInt64(metadata.id.value));
        section.Field("Virtual Path", NormalizePath(metadata.virtualPath));
    }
}

void PaintAsset(HDC dc, RECT content, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const bool deferMeshPreviewWork = sceneContext.HasActiveViewportCameraNavigation();
    int y = content.top;

    if (state.InspectorAsset().IsValid()) {
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(state.InspectorAsset());
        if (metadata == nullptr) {
            DrawEmpty(dc, content, theme);
            return;
        }

        if (metadata->type == "InputAction") {
            PaintInputActionAsset(dc, content, theme, sceneContext, *metadata);
            return;
        }
        if (metadata->type == "InputMappingContext") {
            PaintInputMappingContextAsset(dc, content, theme, sceneContext, *metadata);
            return;
        }
        if (metadata->type == "RenderMaterial") {
            PaintMaterialAsset(dc, content, theme, sceneContext, *metadata);
            return;
        }

        DrawHeader(dc, content, theme, HeroIconKind::Cube, metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name, metadata->type.empty() ? "Asset" : metadata->type);
        y += kHeaderHeight + kPanelPadTop;
        y = DrawTextureDetails(dc, content, y, theme, inspector, *metadata);
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

void PaintAudioSourceSection(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const EditorSceneContext& sceneContext,
    const kb::scene::AudioSourceComponent& audioSource) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::AudioSource, HeroIconKind::Gamepad2, "Audio Source");
    section.Field("Clip", AssetDisplayName(sceneContext, audioSource.clipAssetId), InspectorPropertyId::AudioSourceClip);
    section.Field("Volume", FormatFloat(audioSource.volume, 2), InspectorPropertyId::AudioSourceVolume);
    section.Field("Pitch", FormatFloat(audioSource.pitch, 2), InspectorPropertyId::AudioSourcePitch);
    section.Bool("Enabled", audioSource.enabled, InspectorPropertyId::AudioSourceEnabled);
    section.Bool("Autoplay", audioSource.autoplay, InspectorPropertyId::AudioSourceAutoplay);
    section.Bool("Loop", audioSource.loop, InspectorPropertyId::AudioSourceLoop);
    section.Bool("Mute", audioSource.mute, InspectorPropertyId::AudioSourceMute);
    section.Bool("Spatial", audioSource.spatial, InspectorPropertyId::AudioSourceSpatial);
    section.Field("Attenuation", InspectorComponentLabelFormatter::AudioAttenuationModelName(audioSource.attenuationModel), InspectorPropertyId::AudioSourceAttenuation);
    section.Field("Range", FormatFloat(audioSource.minDistance, 2) + " - " + FormatFloat(audioSource.maxDistance, 2), InspectorPropertyId::AudioSourceRange);
    y = section.Bottom() + kSectionGap;
}

void PaintAudioListenerSection(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const kb::scene::AudioListenerComponent& audioListener) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::AudioListener, HeroIconKind::Gamepad2, "Audio Listener");
    section.Bool("Enabled", audioListener.enabled, InspectorPropertyId::AudioListenerEnabled);
    section.Bool("Primary", audioListener.primary, InspectorPropertyId::AudioListenerPrimary);
    y = section.Bottom() + kSectionGap;
}

void PaintMeshRendererSection(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const EditorSceneContext& sceneContext,
    const kb::scene::MeshRendererComponent& renderer) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::MeshRenderer, HeroIconKind::Cube, "Mesh Renderer");
    section.Field("Mesh", AssetDisplayName(sceneContext, renderer.meshAssetId), InspectorPropertyId::MeshRendererMesh);
    section.Field("Material", MaterialDisplayName(sceneContext, renderer.materialAssetId), InspectorPropertyId::MeshRendererMaterial);
    const int slotRows = MeshRendererMaterialSlotRows(sceneContext, renderer);
    section.Field("Material Slots", std::to_string(slotRows));
    const std::optional<kb::render::RenderMeshAssetData> mesh = LoadMeshAssetData(sceneContext, renderer.meshAssetId);
    for (std::uint32_t slotIndex = 0U; slotIndex < static_cast<std::uint32_t>(slotRows); ++slotIndex) {
        const std::uint64_t materialId = slotIndex < renderer.materialSlotOverrideCount ? renderer.materialSlotAssetIds[slotIndex] : 0U;
        section.Field(MeshRendererMaterialSlotLabel(mesh, slotIndex), MaterialDisplayName(sceneContext, materialId), MeshRendererMaterialSlotProperty(slotIndex));
    }
    section.Bool("Casts Shadow", renderer.castsShadow);
    section.Bool("Receives Shadow", renderer.receivesShadow);
    y = section.Bottom() + kSectionGap;
}

[[nodiscard]] std::size_t AliveSelectionCount(const kb::scene::Scene& scene, std::span<const kb::scene::SceneEntity> selected) noexcept {
    std::size_t count = 0U;
    for (const kb::scene::SceneEntity entity : selected) {
        if (scene.Entities().IsAlive(entity)) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] kb::scene::TransformComponent MultiSelectionTransformView(
    const kb::scene::Scene& scene,
    std::span<const kb::scene::SceneEntity> selected,
    kb::scene::SceneEntity primary) {
    kb::scene::TransformComponent transform = scene.Transforms().Get(primary);
    if (const std::optional<kb::scene::Vec3> pivot = EditorSceneSelectionPivot::Resolve(scene, selected, primary)) {
        transform.localPosition = *pivot;
    }
    return transform;
}

void PaintMultiSelection(HDC dc, RECT content, const EditorTheme& theme, const EditorSceneContext& sceneContext, kb::scene::SceneEntity primary) {
    const kb::scene::Scene& scene = sceneContext.Scene();
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const std::vector<kb::scene::SceneEntity>& selected = sceneContext.SelectedHierarchyEntities();
    const std::size_t aliveCount = AliveSelectionCount(scene, selected);
    const std::string primaryName = scene.Entities().IsAlive(primary) ? scene.Entities().Name(primary) : std::string{ "(none)" };

    DrawHeader(dc, content, theme, HeroIconKind::ListBullet, "Selection (" + std::to_string(aliveCount) + ")", "Primary: " + primaryName);
    int y = content.top + kHeaderHeight + kPanelPadTop;

    {
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::General, HeroIconKind::AdjustmentsHorizontal, "Selection");
        section.Field("Entities", FormatUInt64(aliveCount));
        section.Field("Primary", primaryName);
        section.Field("Primary Id", FormatUInt64(primary.Id()));
        y = section.Bottom() + kSectionGap;
    }

    if (scene.Entities().IsAlive(primary)) {
        const kb::scene::TransformComponent transform = MultiSelectionTransformView(scene, selected, primary);
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Transform, HeroIconKind::Gamepad2, "Transform");
        section.Vec3("Pivot", transform.localPosition, InspectorPropertyId::PositionX, InspectorPropertyId::PositionY, InspectorPropertyId::PositionZ);
        section.Rotation("Primary Rotation", transform.localRotation);
        section.Vec3("Primary Scale", transform.localScale, InspectorPropertyId::ScaleX, InspectorPropertyId::ScaleY, InspectorPropertyId::ScaleZ);
    }
}

void PaintEntity(HDC dc, RECT content, const EditorTheme& theme, const EditorSceneContext& sceneContext, kb::scene::SceneEntity selected) {
    const kb::scene::Scene& scene = sceneContext.Scene();
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const std::string title = scene.Entities().Name(selected);
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

    if (sceneContext.HasEntityScript(selected)) {
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Script, HeroIconKind::CommandLine, "Script", true);
        section.Field("Script", sceneContext.EntityScriptName(selected), InspectorPropertyId::ScriptName);
        section.Bool("Enabled", sceneContext.EntityScriptEnabled(selected), InspectorPropertyId::ScriptEnabled);
        y = section.Bottom() + kSectionGap;
    }
    if (const kb::scene::MeshRendererComponent* meshRenderer = scene.Components().MeshRenderers().TryGet(selected); meshRenderer != nullptr) {
        PaintMeshRendererSection(dc, content, y, theme, inspector, sceneContext, *meshRenderer);
    }
    if (const kb::scene::AudioSourceComponent* audioSource = scene.Components().AudioSources().TryGet(selected); audioSource != nullptr) {
        PaintAudioSourceSection(dc, content, y, theme, inspector, sceneContext, *audioSource);
    }
    if (const kb::scene::AudioListenerComponent* audioListener = scene.Components().AudioListeners().TryGet(selected); audioListener != nullptr) {
        PaintAudioListenerSection(dc, content, y, theme, inspector, *audioListener);
    }
    DrawAddComponent(dc, content, theme, inspector, y);
}

[[nodiscard]] int SectionHeight(const InspectorPanelState& inspector, InspectorSectionId section, int rows) noexcept {
    if (inspector.IsCollapsed(section)) {
        return kSectionHeaderHeight;
    }
    return kSectionHeaderHeight + kDividerHeight + rows * (kFieldRowHeight + kDividerHeight);
}

[[nodiscard]] int InputMappingRows(const EditorSceneContext& sceneContext, kb::assets::AssetId id) {
    const std::optional<kb::input::InputMappingContextAsset> context = sceneContext.ReadInputMappingContextAsset(id);
    return static_cast<int>((context.has_value() ? context->mappings.size() : 0U) * 5U + 1U);
}

[[nodiscard]] int MaterialRows() noexcept {
    return 20;
}

[[nodiscard]] int AssetSectionRows(const EditorSceneContext& sceneContext, const kb::assets::AssetMetadata& metadata) {
    int rows = metadata.importCategory.empty() ? 0 : 1;
    const bool deferMeshPreviewWork = sceneContext.HasActiveViewportCameraNavigation();
    if (!deferMeshPreviewWork) {
        if (const EditorMeshThumbnailStats* stats = EditorMeshPreviewCache().StatsFor(metadata); stats != nullptr) {
            rows += 6;
        }
        if (const EditorMeshValidationResult* validation = EditorMeshPreviewCache().ValidationFor(metadata); validation != nullptr) {
            rows += static_cast<int>(std::min<std::size_t>(validation->issues.size(), 6U));
        }
    }
    rows += 6;
    return rows;
}

[[nodiscard]] int AssetContentHeight(const RECT& content, const EditorSceneContext& sceneContext, const kb::assets::AssetMetadata& metadata) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    int height = kHeaderHeight + kPanelPadTop;
    if (metadata.type == "InputAction") {
        height += SectionHeight(inspector, InspectorSectionId::InputAction, 3) + kSectionGap;
        height += SectionHeight(inspector, InspectorSectionId::Asset, 2);
        return height;
    }
    if (metadata.type == "InputMappingContext") {
        height += SectionHeight(inspector, InspectorSectionId::InputMappings, InputMappingRows(sceneContext, metadata.id));
        return height;
    }
    if (metadata.type == "RenderMaterial") {
        height += SectionHeight(inspector, InspectorSectionId::Material, MaterialRows()) + kSectionGap;
        height += SectionHeight(inspector, InspectorSectionId::Asset, 2);
        return height;
    }

    if (EditorTexturePreviewService::IsTextureAsset(metadata)) {
        const EditorTexturePreviewImage* image = EditorTexturePreviewService::PreviewFor(metadata);
        const int imageHeight = image == nullptr ? 92 : TextureDetailsImageHeight(content, *image);
        height += inspector.IsCollapsed(InspectorSectionId::Details)
            ? kSectionHeaderHeight + kSectionGap
            : kSectionHeaderHeight + kDividerHeight + imageHeight + 20 + kSectionGap;
    }
    if (EditorMeshPreviewCache().StatsFor(metadata) != nullptr) {
        height += MeshPreviewPanelHeight(content) + kSectionGap;
    }
    height += SectionHeight(inspector, InspectorSectionId::Asset, AssetSectionRows(sceneContext, metadata));
    return height;
}

[[nodiscard]] int EntityContentHeight(const EditorSceneContext& sceneContext, kb::scene::SceneEntity selected) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const kb::scene::Scene& scene = sceneContext.Scene();
    int height = kHeaderHeight + kPanelPadTop;
    height += SectionHeight(inspector, InspectorSectionId::General, 2) + kSectionGap;
    height += SectionHeight(inspector, InspectorSectionId::Transform, 3) + kSectionGap;
    if (sceneContext.HasEntityScript(selected)) {
        height += SectionHeight(inspector, InspectorSectionId::Script, 2) + kSectionGap;
    }
    if (const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(selected); renderer != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::MeshRenderer, 5 + MeshRendererMaterialSlotRows(sceneContext, *renderer)) + kSectionGap;
    }
    if (scene.Components().AudioSources().TryGet(selected) != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::AudioSource, 9) + kSectionGap;
    }
    if (scene.Components().AudioListeners().TryGet(selected) != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::AudioListener, 2) + kSectionGap;
    }
    height += kAddComponentButtonHeight;
    if (inspector.IsAddComponentBrowserOpen()) {
        height += 8 + kAddComponentBrowserMaxHeight;
    }
    return height;
}

[[nodiscard]] int MultiSelectionContentHeight(const EditorSceneContext& sceneContext) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    return kHeaderHeight + kPanelPadTop
        + SectionHeight(inspector, InspectorSectionId::General, 3) + kSectionGap
        + SectionHeight(inspector, InspectorSectionId::Transform, 3);
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

void AdvanceRow(int& y) noexcept;

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

[[nodiscard]] InspectorPanelRenderer::Hit HitRows(
    RECT content,
    int& y,
    InspectorSectionId section,
    std::span<const InspectorRowDefinition> rows,
    int x,
    int yPoint) noexcept {
    for (const InspectorRowDefinition& row : rows) {
        const RECT rect = RowRect(content, y);
        const InspectorPanelRenderer::Hit hit = row.kind == InspectorRowValueKind::Bool
            ? HitBool(rect, section, row.property, x, yPoint)
            : HitTextRow(rect, section, row.property, x, yPoint);
        if (hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
    }
    return {};
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

[[nodiscard]] InspectorPanelRenderer::Hit HitSectionHeader(RECT bounds, int& y, const InspectorPanelState& state, InspectorSectionId section, int x, int yPoint, bool removeButton = false) noexcept {
    RECT header = Rect(bounds.left, y, bounds.right, y + kSectionHeaderHeight);
    y += kSectionHeaderHeight;
    if (removeButton) {
        const RECT remove = ComponentRemoveButtonRect(header);
        if (Contains(remove, x, yPoint)) {
            return MakeHit(InspectorHitKind::ComponentMenuButton, section, InspectorPropertyId::ComponentRemove, remove);
        }
    }
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

[[nodiscard]] InspectorPanelRenderer::Hit HitTestInputActionSection(const RECT& content, const InspectorPanelState& state, int x, int yPoint, int& y) {
    if (InspectorPanelRenderer::Hit hit = HitSectionHeader(content, y, state, InspectorSectionId::InputAction, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (state.IsCollapsed(InspectorSectionId::InputAction)) {
        return {};
    }
    // Open Value Type dropdown options overlay the rows beneath, so test them first.
    if (state.IsValueTypeDropdownOpen()) {
        for (int option = 0; option < kValueTypeOptionCount; ++option) {
            const RECT rect = ValueTypeOptionRect(content, option);
            if (x >= rect.left && x < rect.right && yPoint >= rect.top && yPoint < rect.bottom) {
                return InspectorPanelRenderer::Hit{
                    .kind = InspectorHitKind::ValueTypeOption,
                    .section = InspectorSectionId::InputAction,
                    .property = InspectorPropertyId::InputActionValueType,
                    .index = option,
                    .rect = rect,
                };
            }
        }
    }
    const std::array<std::pair<InspectorPropertyId, bool>, 3> rows{ {
        { InspectorPropertyId::InputActionName, false },
        { InspectorPropertyId::InputActionValueType, false },
        { InspectorPropertyId::InputActionConsume, true },
    } };
    for (const auto& [property, isBool] : rows) {
        const RECT row = RowRect(content, y);
        InspectorPanelRenderer::Hit hit = isBool
            ? HitBool(row, InspectorSectionId::InputAction, property, x, yPoint)
            : HitTextRow(row, InspectorSectionId::InputAction, property, x, yPoint);
        if (hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
    }
    return {};
}

[[nodiscard]] InspectorPanelRenderer::Hit HitTestInputMappingSection(const RECT& content, const InspectorPanelState& state, const EditorSceneContext& sceneContext, kb::assets::AssetId imcId, int x, int yPoint, int& y) {
    if (InspectorPanelRenderer::Hit hit = HitSectionHeader(content, y, state, InspectorSectionId::InputMappings, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (state.IsCollapsed(InspectorSectionId::InputMappings)) {
        return {};
    }
    const std::optional<kb::input::InputMappingContextAsset> context = sceneContext.ReadInputMappingContextAsset(imcId);
    const std::size_t mappingCount = context.has_value() ? context->mappings.size() : 0U;
    const std::array<InspectorPropertyId, 5> rowProperties{
        InspectorPropertyId::InputMappingKey,
        InspectorPropertyId::InputMappingAction,
        InspectorPropertyId::InputMappingScale,
        InspectorPropertyId::InputMappingTrigger,
        InspectorPropertyId::InputMappingRemove,
    };
    for (std::size_t mapping = 0; mapping < mappingCount; ++mapping) {
        for (const InspectorPropertyId property : rowProperties) {
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(content, y), InspectorSectionId::InputMappings, property, x, yPoint); hit.kind != InspectorHitKind::None) {
                hit.index = static_cast<int>(mapping);
                return hit;
            }
            AdvanceRow(y);
        }
    }
    if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(content, y), InspectorSectionId::InputMappings, InspectorPropertyId::InputMappingAdd, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    AdvanceRow(y);
    return {};
}

[[nodiscard]] InspectorPanelRenderer::Hit HitTestMaterialSection(const RECT& content, const InspectorPanelState& state, int x, int yPoint, int& y) {
    if (InspectorPanelRenderer::Hit hit = HitSectionHeader(content, y, state, InspectorSectionId::Material, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (state.IsCollapsed(InspectorSectionId::Material)) {
        return {};
    }
    const std::array<InspectorPropertyId, 13> floatRows{ {
        InspectorPropertyId::MaterialBaseColorR,
        InspectorPropertyId::MaterialBaseColorG,
        InspectorPropertyId::MaterialBaseColorB,
        InspectorPropertyId::MaterialBaseColorA,
        InspectorPropertyId::MaterialMetallicFactor,
        InspectorPropertyId::MaterialRoughnessFactor,
        InspectorPropertyId::MaterialNormalScale,
        InspectorPropertyId::MaterialOcclusionStrength,
        InspectorPropertyId::MaterialEmissiveColorR,
        InspectorPropertyId::MaterialEmissiveColorG,
        InspectorPropertyId::MaterialEmissiveColorB,
        InspectorPropertyId::MaterialEmissiveStrength,
        InspectorPropertyId::MaterialAlphaCutoff,
    } };
    for (const InspectorPropertyId property : floatRows) {
        if (InspectorPanelRenderer::Hit hit = HitFloatRow(RowRect(content, y), InspectorSectionId::Material, property, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
    }
    if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(content, y), InspectorSectionId::Material, InspectorPropertyId::MaterialAlphaMode, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    AdvanceRow(y);
    if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(content, y), InspectorSectionId::Material, InspectorPropertyId::MaterialDoubleSided, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    AdvanceRow(y);
    const std::array<InspectorPropertyId, 5> textureRows{ {
        InspectorPropertyId::MaterialAlbedoTexture,
        InspectorPropertyId::MaterialNormalTexture,
        InspectorPropertyId::MaterialMetallicRoughnessTexture,
        InspectorPropertyId::MaterialOcclusionTexture,
        InspectorPropertyId::MaterialEmissiveTexture,
    } };
    for (const InspectorPropertyId property : textureRows) {
        if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(content, y), InspectorSectionId::Material, property, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
    }
    return {};
}

[[nodiscard]] InspectorPanelRenderer::Hit HitTestMultiSelection(const RECT& content, const InspectorPanelState& state, int x, int yPoint, int& y) noexcept {
    if (InspectorPanelRenderer::Hit hit = HitSectionHeader(content, y, state, InspectorSectionId::General, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (!state.IsCollapsed(InspectorSectionId::General)) {
        for (int row = 0; row < 3; ++row) {
            const RECT rect = RowRect(content, y);
            if (Contains(rect, x, yPoint)) {
                return MakeHit(InspectorHitKind::Row, InspectorSectionId::General, InspectorPropertyId::None, rect);
            }
            AdvanceRow(y);
        }
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
    return {};
}

} // namespace

int InspectorPanelRenderer::ContentHeight(const RECT& content, const EditorSceneContext& sceneContext) {
    const EditorAssetBrowserState& assetBrowser = sceneContext.AssetBrowser();
    if (assetBrowser.InspectorAsset().IsValid()) {
        const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetBrowser.InspectorAsset());
        return metadata == nullptr ? RectHeight(content) : AssetContentHeight(content, sceneContext, *metadata);
    }

    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        return RectHeight(content);
    }
    if (sceneContext.SelectedHierarchyEntities().size() > 1U) {
        return MultiSelectionContentHeight(sceneContext);
    }
    return EntityContentHeight(sceneContext, selected);
}

int InspectorPanelRenderer::MaxScrollOffset(const RECT& content, const EditorSceneContext& sceneContext) {
    const RECT viewport = ContentViewportRect(content, false);
    const int contentHeight = ContentHeight(ContentViewportRect(content, true), sceneContext);
    return std::max(0, contentHeight - RectHeight(viewport));
}

RECT InspectorPanelRenderer::ScrollbarTrackRect(const RECT& content) noexcept {
    return RECT{
        .left = content.right - kScrollbarWidth,
        .top = content.top + kScrollbarInset,
        .right = content.right - kScrollbarInset,
        .bottom = content.bottom - kScrollbarInset,
    };
}

RECT InspectorPanelRenderer::ScrollbarThumbRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    const int maxOffset = InspectorPanelRenderer::MaxScrollOffset(content, sceneContext);
    if (maxOffset <= 0) {
        return {};
    }

    const RECT track = ScrollbarTrackRect(content);
    const int trackHeight = RectHeight(track);
    const int viewportHeight = RectHeight(content);
    const int totalHeight = viewportHeight + maxOffset;
    const int thumbHeight = std::clamp((trackHeight * viewportHeight) / std::max(1, totalHeight), kScrollbarMinThumb, std::max(kScrollbarMinThumb, trackHeight));
    const int travel = std::max(0, trackHeight - thumbHeight);
    const int offset = std::clamp(sceneContext.Inspector().ScrollOffset(), 0, maxOffset);
    const int thumbTop = track.top + (travel * offset) / maxOffset;
    return RECT{ track.left + 2, thumbTop, track.right - 2, thumbTop + thumbHeight };
}

void InspectorPanelRenderer::Paint(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext) const {
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, content.left, content.top, content.right, content.bottom);

    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));
    const int maxScroll = MaxScrollOffset(content, sceneContext);
    const bool scrollable = maxScroll > 0;
    const int scroll = std::clamp(sceneContext.Inspector().ScrollOffset(), 0, maxScroll);
    RECT inner = ContentViewportRect(content, scrollable);
    OffsetRect(&inner, 0, -scroll);
    const RECT viewport = ContentViewportRect(content, scrollable);

    SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);

    if (sceneContext.AssetBrowser().InspectorAsset().IsValid()) {
        PaintAsset(dc, inner, theme, sceneContext);
        RestoreDC(dc, -1);
        if (scrollable) {
            const RECT track = ScrollbarTrackRect(content);
            const RECT thumb = ScrollbarThumbRect(content, sceneContext);
            DrawFrame(dc, track, Rgb(18, 20, 24), Rgb(38, 43, 50));
            DrawFrame(dc, thumb, sceneContext.Inspector().IsScrollbarDragging() ? Rgb(104, 116, 130) : Rgb(76, 86, 98), Rgb(94, 105, 118));
        }
        RestoreDC(dc, savedDc);
        return;
    }

    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        DrawEmpty(dc, inner, theme);
        RestoreDC(dc, -1);
        RestoreDC(dc, savedDc);
        return;
    }

    if (sceneContext.SelectedHierarchyEntities().size() > 1U) {
        PaintMultiSelection(dc, inner, theme, sceneContext, selected);
        RestoreDC(dc, -1);
        if (scrollable) {
            const RECT track = ScrollbarTrackRect(content);
            const RECT thumb = ScrollbarThumbRect(content, sceneContext);
            DrawFrame(dc, track, Rgb(18, 20, 24), Rgb(38, 43, 50));
            DrawFrame(dc, thumb, sceneContext.Inspector().IsScrollbarDragging() ? Rgb(104, 116, 130) : Rgb(76, 86, 98), Rgb(94, 105, 118));
        }
        RestoreDC(dc, savedDc);
        return;
    }

    PaintEntity(dc, inner, theme, sceneContext, selected);
    RestoreDC(dc, -1);
    if (scrollable) {
        const RECT track = ScrollbarTrackRect(content);
        const RECT thumb = ScrollbarThumbRect(content, sceneContext);
        DrawFrame(dc, track, Rgb(18, 20, 24), Rgb(38, 43, 50));
        DrawFrame(dc, thumb, sceneContext.Inspector().IsScrollbarDragging() ? Rgb(104, 116, 130) : Rgb(76, 86, 98), Rgb(94, 105, 118));
    }
    RestoreDC(dc, savedDc);
}

InspectorPanelRenderer::Hit InspectorPanelRenderer::HitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int yPoint) noexcept {
    if (!Contains(content, x, yPoint)) {
        return {};
    }
    const int maxScroll = MaxScrollOffset(content, sceneContext);
    const bool scrollable = maxScroll > 0;
    if (scrollable && Contains(ScrollbarTrackRect(content), x, yPoint)) {
        const RECT thumb = ScrollbarThumbRect(content, sceneContext);
        if (Contains(thumb, x, yPoint)) {
            return MakeHit(InspectorHitKind::ScrollbarThumb, InspectorSectionId::None, InspectorPropertyId::None, thumb);
        }
        return MakeHit(InspectorHitKind::ScrollbarTrack, InspectorSectionId::None, InspectorPropertyId::None, ScrollbarTrackRect(content));
    }

    const RECT viewport = ContentViewportRect(content, scrollable);
    if (!Contains(viewport, x, yPoint)) {
        return {};
    }
    const int scrolledY = yPoint + std::clamp(sceneContext.Inspector().ScrollOffset(), 0, maxScroll);

    const InspectorPanelState& state = sceneContext.Inspector();
    int y = content.top + kHeaderHeight + kPanelPadTop;

    if (sceneContext.AssetBrowser().InspectorAsset().IsValid()) {
        const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(sceneContext.AssetBrowser().InspectorAsset());
        if (metadata != nullptr && metadata->type == "InputAction") {
            return HitTestInputActionSection(viewport, state, x, scrolledY, y);
        }
        if (metadata != nullptr && metadata->type == "InputMappingContext") {
            return HitTestInputMappingSection(viewport, state, sceneContext, metadata->id, x, scrolledY, y);
        }
        if (metadata != nullptr && metadata->type == "RenderMaterial") {
            if (InspectorPanelRenderer::Hit hit = HitTestMaterialSection(viewport, state, x, scrolledY, y); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            y += kSectionGap;
            if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::Asset, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            return {};
        }
        if (metadata != nullptr) {
            if (EditorTexturePreviewService::IsTextureAsset(*metadata)) {
                const EditorTexturePreviewImage* image = EditorTexturePreviewService::PreviewFor(*metadata);
                if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::Details, x, scrolledY); hit.kind != InspectorHitKind::None) {
                    return hit;
                }
                if (!state.IsCollapsed(InspectorSectionId::Details)) {
                    const int imageHeight = image == nullptr ? 92 : TextureDetailsImageHeight(content, *image);
                    y += imageHeight + 20 + kSectionGap;
                } else {
                    y += kSectionGap;
                }
            }
            if (EditorMeshPreviewCache().StatsFor(*metadata) != nullptr) {
                const RECT preview = MeshPreviewPanelRect(viewport, y);
                const RECT toolbar = MeshPreviewToolbarRect(preview);
                const std::array<InspectorPropertyId, 4> properties = MeshPreviewToolbarProperties();
                for (int index = 0; index < static_cast<int>(properties.size()); ++index) {
                    const RECT button = MeshPreviewToolbarButtonRect(toolbar, index);
                    if (Contains(button, x, scrolledY)) {
                        return MakeHit(InspectorHitKind::MeshPreviewToolbarButton, InspectorSectionId::Asset, properties[static_cast<std::size_t>(index)], button);
                    }
                }
                if (Contains(preview, x, scrolledY)) {
                    return MakeHit(InspectorHitKind::MeshPreview, InspectorSectionId::Asset, InspectorPropertyId::None, preview);
                }
                y += MeshPreviewPanelHeight(viewport) + kSectionGap;
            }
        }
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::Asset, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        return {};
    }

    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        return {};
    }

    if (sceneContext.SelectedHierarchyEntities().size() > 1U) {
        return HitTestMultiSelection(viewport, state, x, scrolledY, y);
    }

    if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::General, x, scrolledY); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (!state.IsCollapsed(InspectorSectionId::General)) {
        if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::General, InspectorPropertyId::EntityName, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
        if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::General, InspectorPropertyId::EntityVisible, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
    }
    y += kSectionGap;

    if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::Transform, x, scrolledY); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (!state.IsCollapsed(InspectorSectionId::Transform)) {
        if (InspectorPanelRenderer::Hit hit = HitVec3(RowRect(viewport, y), InspectorSectionId::Transform, InspectorPropertyId::PositionX, InspectorPropertyId::PositionY, InspectorPropertyId::PositionZ, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
        if (InspectorPanelRenderer::Hit hit = HitRotation(RowRect(viewport, y), x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
        if (InspectorPanelRenderer::Hit hit = HitVec3(RowRect(viewport, y), InspectorSectionId::Transform, InspectorPropertyId::ScaleX, InspectorPropertyId::ScaleY, InspectorPropertyId::ScaleZ, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
    }
    y += kSectionGap;

    if (sceneContext.HasEntityScript(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::Script, x, scrolledY, true); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (!state.IsCollapsed(InspectorSectionId::Script)) {
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::Script, InspectorPropertyId::ScriptName, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::Script, InspectorPropertyId::ScriptEnabled, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
        }
        y += kSectionGap;
    }

    if (const kb::scene::MeshRendererComponent* renderer = sceneContext.Scene().Components().MeshRenderers().TryGet(selected); renderer != nullptr) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::MeshRenderer, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (!state.IsCollapsed(InspectorSectionId::MeshRenderer)) {
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::MeshRenderer, InspectorPropertyId::MeshRendererMesh, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::MeshRenderer, InspectorPropertyId::MeshRendererMaterial, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::MeshRenderer, InspectorPropertyId::None, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
            const int slotRows = MeshRendererMaterialSlotRows(sceneContext, *renderer);
            for (std::uint32_t slotIndex = 0U; slotIndex < static_cast<std::uint32_t>(slotRows); ++slotIndex) {
                if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::MeshRenderer, MeshRendererMaterialSlotProperty(slotIndex), x, scrolledY); hit.kind != InspectorHitKind::None) {
                    return hit;
                }
                AdvanceRow(y);
            }
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::MeshRenderer, InspectorPropertyId::None, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::MeshRenderer, InspectorPropertyId::None, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
        }
        y += kSectionGap;
    }

    if (sceneContext.Scene().Components().AudioSources().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::AudioSource, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (!state.IsCollapsed(InspectorSectionId::AudioSource)) {
            if (InspectorPanelRenderer::Hit hit = HitRows(viewport, y, InspectorSectionId::AudioSource, kAudioSourceRows, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
        }
        y += kSectionGap;
    }

    if (sceneContext.Scene().Components().AudioListeners().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::AudioListener, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (!state.IsCollapsed(InspectorSectionId::AudioListener)) {
            if (InspectorPanelRenderer::Hit hit = HitRows(viewport, y, InspectorSectionId::AudioListener, kAudioListenerRows, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
        }
        y += kSectionGap;
    }

    const RECT addButton = AddComponentButtonRect(viewport, y);
    if (Contains(addButton, x, scrolledY)) {
        return MakeHit(InspectorHitKind::TextField, InspectorSectionId::AddComponent, InspectorPropertyId::AddComponentButton, addButton);
    }
    if (state.IsAddComponentBrowserOpen()) {
        const RECT browser = AddComponentBrowserRect(viewport, y);
        const RECT search = AddComponentSearchRect(browser);
        if (Contains(search, x, scrolledY)) {
            return MakeHit(InspectorHitKind::TextField, InspectorSectionId::AddComponent, InspectorPropertyId::AddComponentSearch, search);
        }
        const std::vector<const InspectorComponentTile*> tiles = InspectorComponentCatalog::Search(AddComponentQuery(state));
        const RECT results = AddComponentResultsRect(browser);
        int rowTop = results.top;
        std::string category;
        for (std::size_t index = 0; index < tiles.size(); ++index) {
            const InspectorComponentTile* tile = tiles[index];
            if (tile == nullptr) {
                continue;
            }
            if (tile->category != category) {
                category = tile->category;
                rowTop += kAddComponentCategoryHeaderHeight;
            }
            RECT row = Rect(results.left, rowTop, results.right, rowTop + kAddComponentResultRowHeight);
            if (Contains(row, x, scrolledY)) {
                InspectorPanelRenderer::Hit hit = MakeHit(InspectorHitKind::TextField, InspectorSectionId::AddComponent, InspectorPropertyId::AddComponentOption, row);
                hit.index = static_cast<int>(index);
                return hit;
            }
            rowTop += kAddComponentResultRowHeight;
        }
    }
    return {};
}

} // namespace kb::editor

#endif
