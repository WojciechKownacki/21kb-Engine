#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/InspectorPanelSectionRows.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorAddComponentBrowserModel.hpp"
#include "inspection/InspectorComponentLabelFormatter.hpp"
#include "inspection/InspectorMeshRendererMaterialSlotModel.hpp"
#include "inspection/InspectorPhysicsModel.hpp"
#include "inspection/InspectorMaterialTextureSlotFormatter.hpp"
#include "inspection/EditorValueFormatter.hpp"
#include "inspection/MaterialAssetFormatter.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
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
#include "scene/material_preview/EditorMaterialPreviewTelemetry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
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

using inspector_panel_rows::AssetPickerButtonRect;
using inspector_panel_rows::AssetPickerTextRect;
using inspector_panel_rows::CheckboxRectForRow;
using inspector_panel_rows::ComponentRemoveButtonRect;
using inspector_panel_rows::DrawAssetFieldRow;
using inspector_panel_rows::DrawBoolRow;
using inspector_panel_rows::DrawFieldRow;
using inspector_panel_rows::DrawRotationRow;
using inspector_panel_rows::DrawSectionHeader;
using inspector_panel_rows::DrawValueBox;
using inspector_panel_rows::DrawVec3Row;
using inspector_panel_rows::SectionWriter;

// Display/edit text for one exposed script variable's value (Bool rows use the
// dedicated checkbox, so this covers the text-edited types). Floats use %g so a
// 3.5 shows as "3.5" (round-trips cleanly through the edit box) rather than
// std::to_string's "3.500000".
[[nodiscard]] std::string FormatScriptVariableValue(const kb::script::ScriptValue& value, kb::script::ScriptValueType type) {
    switch (type) {
    case kb::script::ScriptValueType::String:
    case kb::script::ScriptValueType::Name:
    case kb::script::ScriptValueType::Guid:
        return value.AsString();
    case kb::script::ScriptValueType::Bool:
        return value.AsBool() ? "true" : "false";
    case kb::script::ScriptValueType::Int:
        return std::to_string(value.AsInt());
    case kb::script::ScriptValueType::Int64:
        return std::to_string(value.AsInt64());
    case kb::script::ScriptValueType::UInt32:
        return std::to_string(value.AsUInt32());
    case kb::script::ScriptValueType::Entity:
    case kb::script::ScriptValueType::Component:
    case kb::script::ScriptValueType::Hash:
        return std::to_string(value.AsUInt64());
    case kb::script::ScriptValueType::Double: {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", value.AsDouble());
        return std::string{ buffer };
    }
    case kb::script::ScriptValueType::Float:
    case kb::script::ScriptValueType::Void:
    default: {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value.AsFloat()));
        return std::string{ buffer };
    }
    }
}

constexpr int kHeaderHeight = 62;
constexpr int kHeaderIcon = 36;
constexpr int kHeaderPad = 8;
constexpr int kPanelPadTop = 10;
constexpr int kSectionGap = 8;
constexpr int kSectionHeaderHeight = 24;
constexpr int kFieldRowHeight = 24;
constexpr int kValueHeight = 20;
constexpr int kRowPadX = 16;
constexpr int kAxisLetterWidth = 11;
constexpr int kAxisGap = 6;
constexpr int kLaneGap = 5;
constexpr int kDividerHeight = 1;
constexpr int kTextBaselineOffsetY = 1;
constexpr int kAssetPreviewMaxHeight = 214;
constexpr int kAssetPreviewMinHeight = 142;
constexpr int kMaterialPreviewHeight = 160;
constexpr int kMaterialPreviewPadding = 12;
constexpr int kMaterialPreviewGap = 8;
constexpr std::size_t kMaterialPreviewMaxMissingRows = 2U;
constexpr int kMeshPreviewToolbarHeight = 30;
constexpr int kMeshPreviewToolbarButtonSize = 22;
constexpr int kMeshPreviewToolbarButtonGap = 4;
constexpr int kAddComponentButtonHeight = 24;
constexpr int kAddComponentBrowserWidth = 240;   // narrow, Unity-style popup width
constexpr int kAddComponentBrowserMaxHeight = 300;
constexpr int kAddComponentSearchHeight = 24;
constexpr int kAddComponentRowHeight = 26;
constexpr int kAddComponentBackHeaderHeight = 24;
constexpr int kAddComponentScrollbarWidth = 10;
constexpr int kAddComponentListTop = 62;         // list start below title + search (categories/search view)
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

[[nodiscard]] float ColorChannel(COLORREF color, int shift) noexcept {
    return static_cast<float>((color >> shift) & 0xFFU) / 255.0F;
}

[[nodiscard]] int ToColorByte(float value) noexcept {
    return std::clamp(static_cast<int>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F)), 0, 255);
}

[[nodiscard]] COLORREF ToColorRef(float red, float green, float blue) noexcept {
    return RGB(ToColorByte(red), ToColorByte(green), ToColorByte(blue));
}

[[nodiscard]] std::uint32_t PackBgra(float red, float green, float blue) noexcept {
    const std::uint32_t r = static_cast<std::uint32_t>(ToColorByte(red));
    const std::uint32_t g = static_cast<std::uint32_t>(ToColorByte(green));
    const std::uint32_t b = static_cast<std::uint32_t>(ToColorByte(blue));
    return b | (g << 8U) | (r << 16U) | 0xFF000000U;
}

[[nodiscard]] std::uint32_t PackBgra(COLORREF color) noexcept {
    return PackBgra(ColorChannel(color, 0), ColorChannel(color, 8), ColorChannel(color, 16));
}

[[nodiscard]] std::uint32_t CompositeOver(std::uint32_t background, float red, float green, float blue, float alpha) noexcept {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    const float inv = 1.0F - alpha;
    const float bgB = static_cast<float>(background & 0xFFU) / 255.0F;
    const float bgG = static_cast<float>((background >> 8U) & 0xFFU) / 255.0F;
    const float bgR = static_cast<float>((background >> 16U) & 0xFFU) / 255.0F;
    return PackBgra((red * alpha) + (bgR * inv), (green * alpha) + (bgG * inv), (blue * alpha) + (bgB * inv));
}

[[nodiscard]] float SmoothCoverage(float signedDistance) noexcept {
    return std::clamp(signedDistance + 0.5F, 0.0F, 1.0F);
}

[[nodiscard]] COLORREF HoverFill(const EditorTheme& theme) noexcept {
    static_cast<void>(theme);
    return Rgb(34, 38, 45);
}

// COLORREF a mixed `percentB`% toward b — matches ProjectFilesPanelDrawing::Blend,
// used to reproduce the Project Files row-hover fill in the Add Component menu.
[[nodiscard]] COLORREF BlendColor(COLORREF a, COLORREF b, int percentB) noexcept {
    const int inv = 100 - percentB;
    return RGB((GetRValue(a) * inv + GetRValue(b) * percentB) / 100,
        (GetGValue(a) * inv + GetGValue(b) * percentB) / 100,
        (GetBValue(a) * inv + GetBValue(b) * percentB) / 100);
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
    return EditorValueFormatter::FormatFloat(value, precision);
}

[[nodiscard]] std::string FormatUInt64(std::uint64_t value) {
    return EditorValueFormatter::FormatUInt64(value);
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

[[nodiscard]] RECT AddComponentButtonRect(RECT content, int y) noexcept {
    const int width = std::min<int>(240, std::max<int>(120, static_cast<int>(content.right - content.left) - 32));
    const int left = content.left + std::max(16, (static_cast<int>(content.right - content.left) - width) / 2);
    return Rect(left, y, left + width, y + kAddComponentButtonHeight);
}

[[nodiscard]] RECT AddComponentBrowserRect(RECT content, int y) noexcept {
    const RECT button = AddComponentButtonRect(content, y);
    const int top = button.bottom + 8;
    // Narrow, fixed-width popup (Unity-style) centred under the button. Always the
    // full height: it is part of the scrollable inspector content (reserved by
    // EntityContentHeight), so the viewport clip + scroll reveal it.
    const int panelWidth = static_cast<int>(content.right - content.left);
    const int width = std::min(kAddComponentBrowserWidth, std::max(160, panelWidth - 24));
    const int left = content.left + std::max(12, (panelWidth - width) / 2);
    const int bottom = top + kAddComponentBrowserMaxHeight;
    return Rect(left, top, left + width, bottom);
}

[[nodiscard]] RECT AddComponentSearchRect(RECT browser) noexcept {
    return Rect(browser.left + 10, browser.top + 34, browser.right - 10, browser.top + 34 + kAddComponentSearchHeight);
}

// The fixed "‹ Category" back header, shown only inside a category (not
// searching). Sits directly below the search box; the list is pushed down past it.
[[nodiscard]] RECT AddComponentBackHeaderRect(RECT browser) noexcept {
    return Rect(browser.left + 1, browser.top + kAddComponentListTop, browser.right - 1, browser.top + kAddComponentListTop + kAddComponentBackHeaderHeight);
}

// The scrollable list area. `showBackHeader` pushes it down past the back header.
[[nodiscard]] RECT AddComponentListRect(RECT browser, bool showBackHeader) noexcept {
    const int top = browser.top + kAddComponentListTop + (showBackHeader ? kAddComponentBackHeaderHeight + 2 : 0);
    return Rect(browser.left + 1, top, browser.right - 1, browser.bottom - 4);
}

// The list area minus the scrollbar gutter (where rows draw), shown only when the
// content overflows.
[[nodiscard]] RECT AddComponentListInnerRect(RECT list, bool scrollable) noexcept {
    if (scrollable) {
        list.right -= kAddComponentScrollbarWidth;
    }
    return list;
}

[[nodiscard]] RECT AddComponentScrollbarTrackRect(RECT list) noexcept {
    return Rect(list.right - kAddComponentScrollbarWidth, list.top, list.right, list.bottom);
}

[[nodiscard]] RECT AddComponentScrollbarThumbRect(RECT list, int rowCount, int scroll) noexcept {
    const int listHeight = static_cast<int>(list.bottom - list.top);
    const int total = InspectorAddComponentBrowserModel::TotalHeight(rowCount, kAddComponentRowHeight);
    if (total <= listHeight || listHeight <= 0) {
        return {};
    }
    const RECT track = AddComponentScrollbarTrackRect(list);
    const int trackHeight = static_cast<int>(track.bottom - track.top);
    const int thumbHeight = std::clamp(trackHeight * listHeight / std::max(1, total), 24, std::max(24, trackHeight));
    const int maxScroll = InspectorAddComponentBrowserModel::MaxScroll(rowCount, kAddComponentRowHeight, listHeight);
    const int travel = std::max(0, trackHeight - thumbHeight);
    const int top = track.top + (maxScroll > 0 ? travel * std::clamp(scroll, 0, maxScroll) / maxScroll : 0);
    return Rect(track.left + 1, top, track.right - 1, top + thumbHeight);
}

[[nodiscard]] std::string_view AddComponentQuery(const InspectorPanelState& inspector) noexcept {
    return inspector.EditedProperty() == InspectorPropertyId::AddComponentSearch ? std::string_view{ inspector.EditBuffer() } : std::string_view{};
}

// Draws one Add Component row: icon (HeroIconPainter, the tab/section icon
// source) + label, a trailing "›" chevron for a category, blue highlight when
// hovered — matching the Unity component picker.
void DrawAddComponentBrowserRow(HDC dc, RECT row, const EditorTheme& theme, HeroIconKind icon, std::string_view label, bool isCategory, bool hovered) {
    if (hovered) {
        // The same subtle row-hover fill as the Project Files list.
        GdiDrawing::FillRectColor(dc, row, BlendColor(Color(theme.strip), Color(theme.textSecondary), 12));
    }
    const int iconSize = 15;
    const int iconTop = row.top + (static_cast<int>(row.bottom - row.top) - iconSize) / 2;
    const RECT iconRect{ row.left + 8, iconTop, row.left + 8 + iconSize, iconTop + iconSize };
    HeroIconPainter::Draw(dc, iconRect, icon, Color(theme.textPrimary), 1);
    Text(dc, Rect(iconRect.right + 8, row.top, row.right - 18, row.bottom), label, Color(theme.textPrimary));
    if (isCategory) {
        const int chevronSize = 12;
        const int chevronTop = row.top + (static_cast<int>(row.bottom - row.top) - chevronSize) / 2;
        HeroIconPainter::Draw(dc, Rect(row.right - 16, chevronTop, row.right - 4, chevronTop + chevronSize), HeroIconKind::ChevronRight, Color(theme.textSecondary), 1);
    }
}

void DrawAddComponentBrowser(HDC dc, RECT content, const EditorTheme& theme, const InspectorPanelState& inspector, int y) {
    const RECT browser = AddComponentBrowserRect(content, y);
    DrawFrame(dc, browser, Rgb(30, 33, 38), Rgb(70, 78, 88));

    {
        ScopedFont titleFont(12, FW_SEMIBOLD);
        const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
        Text(dc, Rect(browser.left + 10, browser.top + 4, browser.right - 10, browser.top + 28), "Add Component", Color(theme.textPrimary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    const RECT search = AddComponentSearchRect(browser);
    const bool searchFocused = inspector.EditedProperty() == InspectorPropertyId::AddComponentSearch;
    DrawFrame(dc, search, Rgb(20, 22, 25), searchFocused ? Color(theme.accent) : Rgb(54, 60, 68));
    const std::string_view query = AddComponentQuery(inspector);
    if (query.empty()) {
        Text(dc, Shrink(search, 24, 0, 8, 0), "Search", Rgb(110, 118, 130));
    } else {
        Text(dc, Shrink(search, 8, 0, 8, 0), query, Color(theme.textPrimary));
    }
    HeroIconPainter::Draw(dc, Rect(search.left + 4, search.top + 5, search.left + 18, search.top + 19), HeroIconKind::MagnifyingGlass, Rgb(120, 128, 140), 1);

    const std::string& category = inspector.AddComponentBrowserCategory();
    const bool showBack = query.empty() && !category.empty();
    if (showBack) {
        const RECT back = AddComponentBackHeaderRect(browser);
        const bool backHovered = inspector.IsHovered(InspectorHitKind::TextField, InspectorSectionId::AddComponent, InspectorPropertyId::AddComponentBack);
        if (backHovered) {
            GdiDrawing::FillRectColor(dc, back, BlendColor(Color(theme.strip), Color(theme.textSecondary), 12));
        }
        const COLORREF backColor = backHovered ? Color(theme.textPrimary) : Color(theme.textSecondary);
        // Back chevron — painted, the exact style/source as the category "›" chevrons.
        const int chevronSize = 12;
        const int chevronTop = back.top + (static_cast<int>(back.bottom - back.top) - chevronSize) / 2;
        HeroIconPainter::Draw(dc, Rect(back.left + 8, chevronTop, back.left + 8 + chevronSize, chevronTop + chevronSize), HeroIconKind::ChevronLeft, backColor, 1);
        // The category's own icon, then its name.
        HeroIconKind categoryIcon = HeroIconKind::Cube;
        for (const InspectorComponentCategory& entry : InspectorComponentCatalog::Categories()) {
            if (entry.name == category) {
                categoryIcon = entry.icon;
                break;
            }
        }
        const int backIconSize = 15;
        const int backIconTop = back.top + (static_cast<int>(back.bottom - back.top) - backIconSize) / 2;
        HeroIconPainter::Draw(dc, Rect(back.left + 26, backIconTop, back.left + 26 + backIconSize, backIconTop + backIconSize), categoryIcon, Color(theme.textPrimary), 1);
        ScopedFont backFont(12, FW_SEMIBOLD);
        const ScopedGdiObject selectedBackFont(dc, backFont.handle);
        Text(dc, Rect(back.left + 48, back.top, back.right - 10, back.bottom), category, Color(theme.textPrimary), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        GdiDrawing::FillRectColor(dc, Rect(back.left, back.bottom - 1, back.right, back.bottom), Rgb(52, 58, 66));
    }

    const std::vector<AddComponentRow> rows = InspectorAddComponentBrowserModel::Rows(query.empty() ? std::string_view{ category } : std::string_view{}, query);
    const RECT list = AddComponentListRect(browser, showBack);
    const int listHeight = static_cast<int>(list.bottom - list.top);
    const int rowCount = static_cast<int>(rows.size());
    const int total = InspectorAddComponentBrowserModel::TotalHeight(rowCount, kAddComponentRowHeight);
    const bool scrollable = total > listHeight;
    const RECT inner = AddComponentListInnerRect(list, scrollable);
    const int maxScroll = InspectorAddComponentBrowserModel::MaxScroll(rowCount, kAddComponentRowHeight, listHeight);
    const int scroll = std::clamp(inspector.AddComponentScroll(), 0, maxScroll);

    // Horizontal slide-in of the incoming level (smoothstep-eased).
    const float slide = inspector.AddComponentSlide();
    const float eased = slide * slide * (3.0F - 2.0F * slide);
    const int slideDir = inspector.AddComponentSlideForward() ? 1 : -1;
    const int offsetX = static_cast<int>((1.0F - eased) * static_cast<float>(inner.right - inner.left) * static_cast<float>(slideDir));

    const int savedListDc = SaveDC(dc);
    IntersectClipRect(dc, list.left, list.top, list.right, list.bottom);
    if (rowCount == 0) {
        Text(dc, list, "No components found", Rgb(122, 130, 144), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        const InspectorAddComponentBrowserModel::VisibleWindow window = InspectorAddComponentBrowserModel::Visible(rowCount, scroll, kAddComponentRowHeight, listHeight);
        for (int i = window.first; i < window.first + window.count; ++i) {
            const int rowTop = list.top - scroll + i * kAddComponentRowHeight;
            const RECT rowRect = Rect(inner.left + offsetX, rowTop, inner.right + offsetX, rowTop + kAddComponentRowHeight);
            const bool hovered = inspector.IsHovered(InspectorHitKind::TextField, InspectorSectionId::AddComponent, InspectorPropertyId::AddComponentOption, i);
            DrawAddComponentBrowserRow(dc, rowRect, theme, rows[static_cast<std::size_t>(i)].icon, rows[static_cast<std::size_t>(i)].label, rows[static_cast<std::size_t>(i)].kind == AddComponentRowKind::Category, hovered);
        }
    }
    RestoreDC(dc, savedListDc);

    if (scrollable) {
        const RECT track = AddComponentScrollbarTrackRect(list);
        DrawFrame(dc, track, Rgb(22, 24, 28), Rgb(40, 45, 52));
        const RECT thumb = AddComponentScrollbarThumbRect(list, rowCount, scroll);
        if (thumb.bottom > thumb.top) {
            DrawFrame(dc, thumb, inspector.IsAddComponentScrollbarDragging() ? Rgb(104, 116, 130) : Rgb(76, 86, 98), Rgb(94, 105, 118));
        }
    }
}

void DrawAddComponent(HDC dc, RECT content, const EditorTheme& theme, const InspectorPanelState& inspector, int y) {
    const RECT button = AddComponentButtonRect(content, y);
    const bool buttonActive = inspector.IsAddComponentBrowserOpen() ||
        inspector.IsHovered(InspectorHitKind::TextField, InspectorSectionId::AddComponent, InspectorPropertyId::AddComponentButton);
    DrawFrame(dc, button, buttonActive ? HoverFill(theme) : Color(theme.chrome), Color(theme.borderPanel));
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
    const kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata,
    bool deferPreviewWork) {
    EditorMeshPreviewService& previews = EditorMeshPreviewCache();
    const EditorMeshPreviewSettings previewSettings = MeshPreviewSettingsFromState(inspector);
    const EditorMeshThumbnailStats* stats = deferPreviewWork ? previews.CachedStatsFor(metadata) : previews.StatsFor(manager, metadata);
    const EditorMeshThumbnailImage* image = deferPreviewWork ? previews.CachedPreviewFor(metadata, previewSettings) : previews.PreviewFor(manager, metadata, previewSettings);
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

[[nodiscard]] bool IsMaterialDocument(const kb::assets::AssetMetadata& metadata) noexcept;

[[nodiscard]] bool IsBuiltInPbrMaterial(const kb::render::RenderMaterialAssetData& material) noexcept {
    const bool builtInType =
        material.materialType.empty() ||
        (material.materialType == kb::render::kRenderMaterialAssetBuiltInPbrType &&
            material.materialTypeVersion == kb::render::kRenderMaterialAssetBuiltInPbrTypeVersion);
    return builtInType && material.materialTypeAssetId == 0U && material.materialTypeAssetPath.empty();
}

[[nodiscard]] std::string MaterialGraphArtifactStatus(const kb::render::RenderMaterialAssetData& material) {
    return material.graph.lastGoodArtifact.IsValid()
        ? "artifact ready #" + std::to_string(material.graph.lastGoodArtifact.assetId)
        : "artifact pending";
}

[[nodiscard]] std::string MaterialGraphDiagnosticStatus(const kb::render::RenderMaterialAssetData& material) {
    const std::vector<kb::render::RenderMaterialGraphDiagnostic> diagnostics = kb::render::ValidateRenderMaterialAssetGraphDiagnostics(material);
    std::size_t errors = 0U;
    std::size_t warnings = 0U;
    for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error) {
            ++errors;
        } else {
            ++warnings;
        }
    }
    if (errors != 0U) {
        return "diagnostics error x" + std::to_string(errors);
    }
    if (warnings != 0U) {
        return "diagnostics warning x" + std::to_string(warnings);
    }
    return "diagnostics ok";
}

[[nodiscard]] std::string MaterialSlotRuntimeStatus(const EditorSceneContext& sceneContext, std::uint64_t id) {
    if (id == 0U) {
        return "None";
    }
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(kb::assets::AssetId{ id });
    if (metadata == nullptr) {
        return "Missing material asset " + std::to_string(id);
    }
    if (!IsMaterialDocument(*metadata)) {
        return "Not a material";
    }
    const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext.ReadMaterialDocumentAsset(metadata->id);
    if (!material.has_value()) {
        return "Unreadable material";
    }
    if (IsBuiltInPbrMaterial(*material)) {
        return "Built-in PBR | runtime ready";
    }

    std::string referenceStatus = "type reference ok";
    if (metadata->type == "RenderMaterial") {
        const kb::render::RenderMaterialTypeReferenceValidationResult reference =
            kb::render::ValidateRenderMaterialTypeReference(*material, *metadata, manager);
        if (!reference.Succeeded()) {
            referenceStatus = "type reference error x" + std::to_string(reference.diagnostics.size());
        }
    }

    return "Graph-backed | "
        + referenceStatus
        + " | "
        + MaterialGraphDiagnosticStatus(*material)
        + " | "
        + MaterialGraphArtifactStatus(*material);
}

[[nodiscard]] std::optional<kb::render::RenderMeshAssetData> LoadMeshAssetData(const EditorSceneContext& sceneContext, std::uint64_t meshAssetId) {
    if (meshAssetId == 0U) {
        return std::nullopt;
    }
    // Resolve through the AssetManager's cache (mount-aware, and a cache hit for
    // any mesh the viewport is already rendering) rather than re-opening and
    // re-parsing the mesh file from disk on every paint AND every hit-test — that
    // per-call synchronous disk load was the Inspector's steady-state hitch.
    kb::assets::AssetManager& manager = const_cast<kb::assets::AssetManager&>(sceneContext.Scene().Assets().Manager());
    const kb::assets::AssetHandle<kb::render::RenderMeshAssetData> asset =
        manager.Load<kb::render::RenderMeshAssetData>(kb::assets::AssetId{ meshAssetId });
    if (!asset.IsLoaded()) {
        return std::nullopt;
    }
    return *asset;
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

[[nodiscard]] InspectorPropertyId MeshRendererMaterialSlotPickerProperty(std::uint32_t slotIndex) noexcept {
    switch (slotIndex) {
    case 0U:
        return InspectorPropertyId::MeshRendererMaterialSlotPicker0;
    case 1U:
        return InspectorPropertyId::MeshRendererMaterialSlotPicker1;
    case 2U:
        return InspectorPropertyId::MeshRendererMaterialSlotPicker2;
    case 3U:
        return InspectorPropertyId::MeshRendererMaterialSlotPicker3;
    case 4U:
        return InspectorPropertyId::MeshRendererMaterialSlotPicker4;
    case 5U:
        return InspectorPropertyId::MeshRendererMaterialSlotPicker5;
    case 6U:
        return InspectorPropertyId::MeshRendererMaterialSlotPicker6;
    case 7U:
        return InspectorPropertyId::MeshRendererMaterialSlotPicker7;
    default:
        return InspectorPropertyId::None;
    }
}

[[nodiscard]] std::string AlphaModeName(kb::render::RenderMaterialAlphaMode mode) {
    return MaterialAssetFormatter::AlphaModeName(mode);
}

[[nodiscard]] bool IsMaterialDocument(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance";
}

[[nodiscard]] std::string_view MaterialDocumentLabel(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterialInstance" ? std::string_view{ "Material Instance" } : std::string_view{ "Render Material" };
}

[[nodiscard]] EditorMaterialPreviewTelemetry MaterialPreviewTelemetryFor(const EditorSceneContext& sceneContext, const kb::assets::AssetMetadata& metadata) {
    const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext.ReadMaterialDocumentAsset(metadata.id);
    return EditorMaterialPreviewTelemetryBuilder::Build(
        sceneContext.Scene().Assets().Manager(),
        metadata.id,
        material.has_value() ? &*material : nullptr,
        true);
}

[[nodiscard]] std::vector<MaterialDebugChannelRow> MaterialDebugChannelRowsFor(const EditorSceneContext& sceneContext, const kb::assets::AssetMetadata& metadata) {
    const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext.ReadMaterialDocumentAsset(metadata.id);
    return material.has_value()
        ? MaterialAssetFormatter::DebugChannelRows(material->desc, metadata.id.value)
        : std::vector<MaterialDebugChannelRow>{};
}

[[nodiscard]] int MaterialPreviewRowCount(const EditorMaterialPreviewTelemetry& telemetry, std::size_t debugChannelRowCount) noexcept {
    const int debugRows = debugChannelRowCount == 0U ? 1 : static_cast<int>(debugChannelRowCount);
    return debugRows + 3 + static_cast<int>(std::min<std::size_t>(telemetry.missingTextures.size(), kMaterialPreviewMaxMissingRows));
}

[[nodiscard]] int MaterialPreviewBodyHeight(const EditorMaterialPreviewTelemetry& telemetry, std::size_t debugChannelRowCount) noexcept {
    return kDividerHeight
        + kMaterialPreviewGap
        + kMaterialPreviewHeight
        + kMaterialPreviewGap
        + MaterialPreviewRowCount(telemetry, debugChannelRowCount) * (kFieldRowHeight + kDividerHeight);
}

[[nodiscard]] int MaterialPreviewSectionHeight(const InspectorPanelState& inspector, const EditorMaterialPreviewTelemetry& telemetry, std::size_t debugChannelRowCount) noexcept {
    if (inspector.IsCollapsed(InspectorSectionId::MaterialPreview)) {
        return kSectionHeaderHeight;
    }
    return kSectionHeaderHeight + MaterialPreviewBodyHeight(telemetry, debugChannelRowCount);
}

struct InspectorMaterialPreviewStyle {
    COLORREF baseColor = RGB(104, 126, 130);
    COLORREF emissiveColor = RGB(0, 0, 0);
    float roughness = 0.65F;
    float metallic = 0.0F;
    float emissiveStrength = 0.0F;
    bool loaded = false;
};

[[nodiscard]] InspectorMaterialPreviewStyle MaterialPreviewStyleFor(const std::optional<kb::render::RenderMaterialAssetData>& material) noexcept {
    InspectorMaterialPreviewStyle style{};
    if (!material.has_value()) {
        return style;
    }
    style.baseColor = ToColorRef(material->desc.baseColor[0], material->desc.baseColor[1], material->desc.baseColor[2]);
    style.emissiveColor = ToColorRef(material->desc.emissiveColor[0], material->desc.emissiveColor[1], material->desc.emissiveColor[2]);
    style.roughness = std::clamp(material->desc.roughnessFactor, 0.0F, 1.0F);
    style.metallic = std::clamp(material->desc.metallicFactor, 0.0F, 1.0F);
    style.emissiveStrength = std::clamp(material->desc.emissiveStrength, 0.0F, 64.0F);
    style.loaded = true;
    return style;
}

void DrawStaticMaterialPreview(HDC dc, RECT frame, const InspectorMaterialPreviewStyle& style) {
    frame = Shrink(frame, 1, 1, 1, 1);
    const int width = RectWidth(frame);
    const int height = RectHeight(frame);
    if (width <= 4 || height <= 4) {
        return;
    }

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), PackBgra(RGB(7, 8, 10)));
    const COLORREF baseColor = style.loaded ? style.baseColor : RGB(104, 126, 130);
    const float baseR = ColorChannel(baseColor, 0);
    const float baseG = ColorChannel(baseColor, 8);
    const float baseB = ColorChannel(baseColor, 16);
    const float emissiveR = ColorChannel(style.emissiveColor, 0) * style.emissiveStrength;
    const float emissiveG = ColorChannel(style.emissiveColor, 8) * style.emissiveStrength;
    const float emissiveB = ColorChannel(style.emissiveColor, 16) * style.emissiveStrength;

    const float radius = static_cast<float>(std::max(12, std::min(width - 24, height - 22))) * 0.5F;
    const float centerX = static_cast<float>(width) * 0.5F;
    const float centerY = static_cast<float>(height) * 0.50F;
    const float shadowCenterY = centerY + radius * 0.78F;
    const float shadowRx = radius * 0.86F;
    const float shadowRy = std::max(2.0F, radius * 0.18F);
    const float roughness = std::clamp(style.roughness, 0.0F, 1.0F);
    const float metal = std::clamp(style.metallic, 0.0F, 1.0F);

    constexpr float lightX = -0.46F;
    constexpr float lightY = -0.62F;
    constexpr float lightZ = 0.63F;
    constexpr float halfX = -0.27F;
    constexpr float halfY = -0.36F;
    constexpr float halfZ = 0.89F;

    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const float px = static_cast<float>(x) + 0.5F;
            const float py = static_cast<float>(y) + 0.5F;

            const float shadowDx = (px - (centerX + radius * 0.10F)) / shadowRx;
            const float shadowDy = (py - shadowCenterY) / shadowRy;
            const float shadowDistance = shadowDx * shadowDx + shadowDy * shadowDy;
            if (shadowDistance < 1.0F) {
                const float shadowAlpha = std::pow(1.0F - shadowDistance, 1.35F) * 0.30F;
                pixels[index] = CompositeOver(pixels[index], 0.02F, 0.025F, 0.032F, shadowAlpha);
            }

            const float nx = (px - centerX) / radius;
            const float ny = (py - centerY) / radius;
            const float distance2 = nx * nx + ny * ny;
            if (distance2 > 1.08F) {
                continue;
            }

            const float distance = std::sqrt(distance2);
            const float coverage = SmoothCoverage((1.0F - distance) * radius);
            if (coverage <= 0.0F) {
                continue;
            }

            const float nz = std::sqrt(std::max(0.0F, 1.0F - distance2));
            const float diffuse = std::max(0.0F, (nx * lightX) + (ny * lightY) + (nz * lightZ));
            const float lowerShade = 1.0F - std::max(0.0F, ny) * 0.30F;
            const float rim = std::pow(std::clamp(1.0F - nz, 0.0F, 1.0F), 1.80F) * 0.20F;
            const float specPower = 84.0F - roughness * 62.0F;
            const float specular = std::pow(std::max(0.0F, (nx * halfX) + (ny * halfY) + (nz * halfZ)), specPower) * (0.42F + metal * 0.34F - roughness * 0.22F);
            const float sheen = std::pow(std::max(0.0F, (-nx * 0.35F) + (-ny * 0.72F) + (nz * 0.60F)), 18.0F) * 0.10F;
            const float shade = (0.34F + diffuse * 0.66F) * lowerShade;

            float red = (baseR * shade) + emissiveR + (rim * 0.22F) + specular + sheen;
            float green = (baseG * shade) + emissiveG + (rim * 0.22F) + specular + sheen;
            float blue = (baseB * shade) + emissiveB + (rim * 0.24F) + specular + sheen;

            const float edgeDarken = std::clamp((distance - 0.78F) / 0.22F, 0.0F, 1.0F) * 0.24F;
            red *= 1.0F - edgeDarken;
            green *= 1.0F - edgeDarken;
            blue *= 1.0F - edgeDarken;

            pixels[index] = CompositeOver(pixels[index], red, green, blue, coverage);
        }
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    static_cast<void>(StretchDIBits(dc, frame.left, frame.top, width, height, 0, 0, width, height, pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY));
}

void DrawTelemetryRow(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    std::string_view label,
    std::string_view value) {
    DrawFieldRow(dc, Rect(content.left, y, content.right, y + kFieldRowHeight), theme, inspector, InspectorSectionId::MaterialPreview, InspectorPropertyId::None, label, value);
    y += kFieldRowHeight;
    DrawDivider(dc, content.left, content.right, y);
    y += kDividerHeight;
}

[[nodiscard]] int DrawMaterialPreview(
    HDC dc,
    RECT content,
    int y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const EditorSceneContext& sceneContext,
    const kb::assets::AssetMetadata& metadata) {
    const EditorMaterialPreviewTelemetry telemetry = MaterialPreviewTelemetryFor(sceneContext, metadata);
    DrawSectionHeader(dc, Rect(content.left, y, content.right, y + kSectionHeaderHeight), theme, inspector, InspectorSectionId::MaterialPreview, HeroIconKind::Eye, "Preview");
    y += kSectionHeaderHeight;
    if (inspector.IsCollapsed(InspectorSectionId::MaterialPreview)) {
        return y + kSectionGap;
    }

    const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext.ReadMaterialDocumentAsset(metadata.id);
    const std::vector<MaterialDebugChannelRow> debugRows = material.has_value()
        ? MaterialAssetFormatter::DebugChannelRows(material->desc, metadata.id.value)
        : std::vector<MaterialDebugChannelRow>{};
    DrawDivider(dc, content.left, content.right, y);
    y += kDividerHeight;
    const RECT frame = Rect(content.left + kMaterialPreviewPadding, y + kMaterialPreviewGap, content.right - kMaterialPreviewPadding, y + kMaterialPreviewGap + kMaterialPreviewHeight);
    DrawFrame(dc, frame, Rgb(13, 15, 18), Color(theme.borderPanel));
    DrawStaticMaterialPreview(dc, frame, MaterialPreviewStyleFor(material));
    y = frame.bottom + kMaterialPreviewGap;

    if (debugRows.empty()) {
        DrawTelemetryRow(dc, content, y, theme, inspector, "Material Id", FormatUInt64(telemetry.materialAssetId.value));
    } else {
        for (const MaterialDebugChannelRow& row : debugRows) {
            DrawTelemetryRow(dc, content, y, theme, inspector, row.label, row.value);
        }
    }
    DrawTelemetryRow(dc, content, y, theme, inspector, "Cache", telemetry.materialLoaded ? "Loaded" : "Missing");
    DrawTelemetryRow(dc, content, y, theme, inspector, "Preview Scene", telemetry.previewSceneReady ? "Ready" : "Fallback");
    DrawTelemetryRow(dc, content, y, theme, inspector, "Missing Textures", FormatUInt64(telemetry.missingTextureCount));
    const std::size_t shown = std::min<std::size_t>(telemetry.missingTextures.size(), kMaterialPreviewMaxMissingRows);
    for (std::size_t index = 0; index < shown; ++index) {
        DrawTelemetryRow(dc, content, y, theme, inspector, index == 0U ? "Diagnostic" : "", telemetry.missingTextures[index]);
    }
    return y + kSectionGap;
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
    const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext.ReadMaterialDocumentAsset(metadata.id);

    const std::string headerLabel = std::string{ MaterialDocumentLabel(metadata) } + (sceneContext.HasDirtyMaterialAssetEdit() ? " *" : "");
    DrawHeader(dc, content, theme, HeroIconKind::Cube, metadata.name.empty() ? metadata.virtualPath.filename().string() : metadata.name, headerLabel);
    int y = content.top + kHeaderHeight + kPanelPadTop;
    y = DrawMaterialPreview(dc, content, y, theme, inspector, sceneContext, metadata);
    if (!material.has_value()) {
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Asset, HeroIconKind::Cube, "Asset");
        section.Field("Status", "Material source could not be read.");
        section.Field("Id", FormatUInt64(metadata.id.value));
        section.Field("Virtual Path", NormalizePath(metadata.virtualPath));
        return;
    }
    {
        const auto property = [](InspectorPropertyId) noexcept {
            return InspectorPropertyId::None;
        };
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Material, HeroIconKind::AdjustmentsHorizontal, "Material");
        section.Float("Base R", FormatFloat(material->desc.baseColor[0]), property(InspectorPropertyId::MaterialBaseColorR));
        section.Float("Base G", FormatFloat(material->desc.baseColor[1]), property(InspectorPropertyId::MaterialBaseColorG));
        section.Float("Base B", FormatFloat(material->desc.baseColor[2]), property(InspectorPropertyId::MaterialBaseColorB));
        section.Float("Base A", FormatFloat(material->desc.baseColor[3]), property(InspectorPropertyId::MaterialBaseColorA));
        section.Float("Metallic", FormatFloat(material->desc.metallicFactor), property(InspectorPropertyId::MaterialMetallicFactor));
        section.Float("Roughness", FormatFloat(material->desc.roughnessFactor), property(InspectorPropertyId::MaterialRoughnessFactor));
        section.Float("Normal Scale", FormatFloat(material->desc.normalScale), property(InspectorPropertyId::MaterialNormalScale));
        section.Float("Occlusion", FormatFloat(material->desc.occlusionStrength), property(InspectorPropertyId::MaterialOcclusionStrength));
        section.Float("Emissive R", FormatFloat(material->desc.emissiveColor[0]), property(InspectorPropertyId::MaterialEmissiveColorR));
        section.Float("Emissive G", FormatFloat(material->desc.emissiveColor[1]), property(InspectorPropertyId::MaterialEmissiveColorG));
        section.Float("Emissive B", FormatFloat(material->desc.emissiveColor[2]), property(InspectorPropertyId::MaterialEmissiveColorB));
        section.Float("Emissive Strength", FormatFloat(material->desc.emissiveStrength), property(InspectorPropertyId::MaterialEmissiveStrength));
        section.Float("Alpha Cutoff", FormatFloat(material->desc.alphaCutoff), property(InspectorPropertyId::MaterialAlphaCutoff));
        section.Field("Alpha Mode", AlphaModeName(material->desc.alphaMode), property(InspectorPropertyId::MaterialAlphaMode));
        section.Bool("Double Sided", material->desc.doubleSided, property(InspectorPropertyId::MaterialDoubleSided));
        const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
        section.Field("Albedo", InspectorMaterialTextureSlotFormatter::DisplayName(manager, material->desc.albedoTextureAssetId), property(InspectorPropertyId::MaterialAlbedoTexture));
        section.Field("Normal", InspectorMaterialTextureSlotFormatter::DisplayName(manager, material->desc.normalTextureAssetId), property(InspectorPropertyId::MaterialNormalTexture));
        section.Field("Metallic-Roughness", InspectorMaterialTextureSlotFormatter::DisplayName(manager, material->desc.metallicRoughnessTextureAssetId), property(InspectorPropertyId::MaterialMetallicRoughnessTexture));
        section.Field("Occlusion", InspectorMaterialTextureSlotFormatter::DisplayName(manager, material->desc.occlusionTextureAssetId), property(InspectorPropertyId::MaterialOcclusionTexture));
        section.Field("Emissive", InspectorMaterialTextureSlotFormatter::DisplayName(manager, material->desc.emissiveTextureAssetId), property(InspectorPropertyId::MaterialEmissiveTexture));
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
        if (IsMaterialDocument(*metadata)) {
            PaintMaterialAsset(dc, content, theme, sceneContext, *metadata);
            return;
        }

        DrawHeader(dc, content, theme, HeroIconKind::Cube, metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name, metadata->type.empty() ? "Asset" : metadata->type);
        y += kHeaderHeight + kPanelPadTop;
        y = DrawTextureDetails(dc, content, y, theme, inspector, *metadata);
        y = DrawMeshPreview(dc, content, y, theme, inspector, manager, *metadata, deferMeshPreviewWork);
        SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Asset, HeroIconKind::Cube, "Asset");
        if (!metadata->importCategory.empty()) {
            section.Field("Category", metadata->importCategory);
        }
        if (!deferMeshPreviewWork) {
            if (const EditorMeshThumbnailStats* stats = EditorMeshPreviewCache().StatsFor(manager, *metadata)) {
                section.Field("Vertices", FormatUInt64(stats->vertexCount));
                section.Field("Indices", FormatUInt64(stats->indexCount));
                section.Field("Triangles", FormatUInt64(stats->triangleCount));
                section.Field("Material Slots", FormatUInt64(stats->materialSlotCount));
                section.Field("Bounds Center", FormatVec3(stats->boundsCenter));
                section.Field("Bounds Radius", FormatFloat(stats->boundsRadius, 3));
            }
            if (const EditorMeshValidationResult* validation = EditorMeshPreviewCache().ValidationFor(manager, *metadata)) {
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
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::AudioSource, HeroIconKind::SpeakerWave, "Audio Source");
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
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::AudioListener, HeroIconKind::SpeakerWave, "Audio Listener");
    section.Bool("Enabled", audioListener.enabled, InspectorPropertyId::AudioListenerEnabled);
    section.Bool("Primary", audioListener.primary, InspectorPropertyId::AudioListenerPrimary);
    y = section.Bottom() + kSectionGap;
}

[[nodiscard]] bool LightUsesRange(kb::scene::LightKind kind) noexcept {
    return kind != kb::scene::LightKind::Directional;
}

[[nodiscard]] bool LightUsesSpotCone(kb::scene::LightKind kind) noexcept {
    return kind == kb::scene::LightKind::Spot;
}

[[nodiscard]] bool LightUsesAreaSize(kb::scene::LightKind kind) noexcept {
    return kind == kb::scene::LightKind::AreaRect || kind == kb::scene::LightKind::AreaDisk || kind == kb::scene::LightKind::Tube;
}

[[nodiscard]] int LightSectionRows(const kb::scene::LightComponent& light) noexcept {
    int rows = 8;
    if (LightUsesRange(light.kind)) {
        ++rows;
    }
    if (LightUsesSpotCone(light.kind)) {
        rows += 2;
    }
    if (LightUsesAreaSize(light.kind)) {
        rows += 2;
    }
    return rows;
}

void PaintLightSection(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const kb::scene::LightComponent& light) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Light, HeroIconKind::Bolt, "Light");
    section.Field("Type", InspectorComponentLabelFormatter::LightKindName(light.kind), InspectorPropertyId::LightKind);
    section.Float("Color R", FormatFloat(light.color.x, 2), InspectorPropertyId::LightColorR);
    section.Float("Color G", FormatFloat(light.color.y, 2), InspectorPropertyId::LightColorG);
    section.Float("Color B", FormatFloat(light.color.z, 2), InspectorPropertyId::LightColorB);
    section.Float("Intensity", FormatFloat(light.intensity, 2), InspectorPropertyId::LightIntensity);
    if (LightUsesRange(light.kind)) {
        section.Float("Range", FormatFloat(light.range, 2), InspectorPropertyId::LightRange);
    }
    if (LightUsesSpotCone(light.kind)) {
        section.Float("Inner Cone", FormatFloat(light.innerConeDegrees, 2), InspectorPropertyId::LightInnerCone);
        section.Float("Outer Cone", FormatFloat(light.outerConeDegrees, 2), InspectorPropertyId::LightOuterCone);
    }
    if (LightUsesAreaSize(light.kind)) {
        section.Float("Area Width", FormatFloat(light.areaWidth, 2), InspectorPropertyId::LightAreaWidth);
        section.Float("Area Height", FormatFloat(light.areaHeight, 2), InspectorPropertyId::LightAreaHeight);
    }
    section.Float("Contact Shadow", FormatFloat(light.contactShadowLength, 2), InspectorPropertyId::LightContactShadowLength);
    section.Float("Volumetric", FormatFloat(light.volumetricScattering, 2), InspectorPropertyId::LightVolumetricScattering);
    section.Bool("Casts Shadow", light.castsShadow, InspectorPropertyId::LightCastsShadow);
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
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::MeshRenderer, HeroIconKind::Cube, "Mesh Renderer", true);
    section.AssetField("Mesh", AssetDisplayName(sceneContext, renderer.meshAssetId), InspectorPropertyId::MeshRendererMesh, InspectorPropertyId::MeshRendererMeshPicker);
    section.AssetField("Material", MaterialDisplayName(sceneContext, renderer.materialAssetId), InspectorPropertyId::MeshRendererMaterial, InspectorPropertyId::MeshRendererMaterialPicker);
    const std::optional<kb::render::RenderMeshAssetData> mesh = LoadMeshAssetData(sceneContext, renderer.meshAssetId);
    const std::vector<InspectorMeshRendererMaterialSlotRow> slotRows = InspectorMeshRendererMaterialSlotModel::Build(
        renderer,
        mesh,
        [&sceneContext](std::uint64_t materialId) { return MaterialDisplayName(sceneContext, materialId); },
        [&sceneContext](std::uint64_t materialId) { return MaterialSlotRuntimeStatus(sceneContext, materialId); });
    for (const InspectorMeshRendererMaterialSlotRow& row : slotRows) {
        const std::string prefix = "Slot " + std::to_string(row.slotIndex + 1U);
        section.Field(prefix + " Name", row.slotName);
        section.Field(prefix + " Source", row.importedSourceName);
        section.Field(prefix + " Default", row.defaultMaterialName);
        section.AssetField(row.label, row.overrideMaterialName, MeshRendererMaterialSlotProperty(row.slotIndex), MeshRendererMaterialSlotPickerProperty(row.slotIndex));
        section.Field(prefix + " Material Status", row.activeMaterialStatus);
        section.Field(prefix + " Sections", row.sectionsUsingSlot);
    }
    section.Bool("Casts Shadow", renderer.castsShadow);
    section.Bool("Receives Shadow", renderer.receivesShadow);
    y = section.Bottom() + kSectionGap;
}

// Renders any physics component as an index-addressed list of PhysicsField rows
// (from InspectorPhysicsModel): a checkbox for Bool fields, an editable text row
// for Float and (click-to-cycle) Enum fields. The section header carries the "×"
// remove affordance like Script/Mesh Renderer. Every editable row shares one
// InspectorPropertyId::PhysicsField id; the row index identifies the field.
// The centred "Fit to Mesh" button rect within a collider-section action row.
[[nodiscard]] RECT ColliderFitButtonRect(RECT row) noexcept {
    const int margin = std::min(48, static_cast<int>(row.right - row.left) / 5);
    return Rect(row.left + margin, row.top + 2, row.right - margin, row.bottom - 2);
}

void PaintPhysicsSection(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    InspectorSectionId sectionId,
    std::string_view title,
    InspectorPropertyId fieldProperty,
    const std::vector<PhysicsField>& fields,
    bool showFitButton,
    bool fitEnabled) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, sectionId, HeroIconKind::Cube, title, true);
    for (int index = 0; index < static_cast<int>(fields.size()); ++index) {
        const PhysicsField& field = fields[static_cast<std::size_t>(index)];
        // Pass the row index so hover/inline-edit highlight only the row under the
        // cursor (all rows in a component share one property id).
        if (field.kind == PhysicsFieldKind::Bool) {
            section.Bool(field.label, field.boolValue, fieldProperty, index);
        } else {
            section.Field(field.label, field.value, fieldProperty, index);
        }
    }
    int bottom = section.Bottom();
    if (showFitButton && !inspector.IsCollapsed(sectionId)) {
        const RECT button = ColliderFitButtonRect(Rect(content.left, bottom, content.right, bottom + kFieldRowHeight));
        const bool hovered = fitEnabled && inspector.IsHovered(InspectorHitKind::TextField, sectionId, InspectorPropertyId::ColliderFitToMesh);
        const COLORREF fill = !fitEnabled ? Rgb(30, 33, 38) : (hovered ? BlendColor(Color(theme.accent), Rgb(0, 0, 0), 15) : Rgb(38, 43, 50));
        const COLORREF border = fitEnabled ? Color(theme.accent) : Rgb(52, 58, 66);
        DrawFrame(dc, button, fill, border);
        ScopedFont buttonFont(12, FW_SEMIBOLD);
        const ScopedGdiObject selectedButtonFont(dc, buttonFont.handle);
        const int iconSize = 13;
        const int iconTop = button.top + (static_cast<int>(button.bottom - button.top) - iconSize) / 2;
        const COLORREF textColor = fitEnabled ? Color(theme.textPrimary) : Rgb(96, 104, 116);
        HeroIconPainter::Draw(dc, Rect(button.left + 14, iconTop, button.left + 14 + iconSize, iconTop + iconSize), HeroIconKind::Cube, textColor, 1);
        Text(dc, Rect(button.left + 14 + iconSize, button.top, button.right - 10, button.bottom), "Fit to Mesh", textColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        bottom += kFieldRowHeight + kDividerHeight;
    }
    y = bottom + kSectionGap;
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

// Forward declarations — the layout-height helpers are defined further down but
// PaintEntity needs them to size (and virtualize) each section.
[[nodiscard]] int SectionHeight(const InspectorPanelState& inspector, InspectorSectionId section, int rows) noexcept;
[[nodiscard]] int MeshRendererRowCount(const EditorSceneContext& sceneContext, const kb::scene::MeshRendererComponent& renderer);

void PaintEntity(HDC dc, RECT content, const RECT& viewport, const EditorTheme& theme, const EditorSceneContext& sceneContext, kb::scene::SceneEntity selected) {
    const kb::scene::Scene& scene = sceneContext.Scene();
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const std::string title = scene.Entities().Name(selected);
    const std::string subtitle = "Entity " + FormatUInt64(selected.Id());

    DrawHeader(dc, content, theme, HeroIconKind::Cube, title, subtitle);
    int y = content.top + kHeaderHeight + kPanelPadTop;

    // Virtualization: a section is only painted when its [y, y+height] band
    // intersects the visible viewport. The cursor advances by the section's
    // measured height whether or not it painted, so culling can never shift the
    // layout — off-screen sections just skip their (clipped-away) GDI work.
    const auto sectionVisible = [&viewport](int top, int height) noexcept {
        return top < viewport.bottom && top + height > viewport.top;
    };

    const kb::scene::VisibilityComponent visibility = scene.Components().Visibility().Get(selected);
    {
        const int h = SectionHeight(inspector, InspectorSectionId::General, 2);
        if (sectionVisible(y, h)) {
            SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::General, HeroIconKind::AdjustmentsHorizontal, "General");
            section.Field("Name", scene.Entities().Name(selected), InspectorPropertyId::EntityName);
            section.Bool("Visible", visibility.visible, InspectorPropertyId::EntityVisible);
        }
        y += h + kSectionGap;
    }

    const kb::scene::TransformComponent transform = scene.Transforms().Get(selected);
    {
        const int h = SectionHeight(inspector, InspectorSectionId::Transform, 3);
        if (sectionVisible(y, h)) {
            SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Transform, HeroIconKind::Gamepad2, "Transform");
            section.Vec3("Position", transform.localPosition, InspectorPropertyId::PositionX, InspectorPropertyId::PositionY, InspectorPropertyId::PositionZ);
            section.Rotation("Rotation", transform.localRotation);
            section.Vec3("Scale", transform.localScale, InspectorPropertyId::ScaleX, InspectorPropertyId::ScaleY, InspectorPropertyId::ScaleZ);
        }
        y += h + kSectionGap;
    }

    if (sceneContext.HasEntityScript(selected)) {
        const std::vector<EditorSceneContext::EntityScriptVariable> scriptVariables = sceneContext.EntityScriptExposedVariables(selected);
        const int h = SectionHeight(inspector, InspectorSectionId::Script, 2 + static_cast<int>(scriptVariables.size()));
        if (sectionVisible(y, h)) {
            SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Script, HeroIconKind::CommandLine, "Script", true);
            section.AssetField("Script", sceneContext.EntityScriptName(selected), InspectorPropertyId::ScriptName, InspectorPropertyId::ScriptPicker);
            section.Bool("Enabled", sceneContext.EntityScriptEnabled(selected), InspectorPropertyId::ScriptEnabled);
            // One editable row per exposed ("@expose") variable. A leading dot marks
            // an overridden variable (its value differs from the script default) — the
            // Godot/O3DE override affordance.
            for (int index = 0; index < static_cast<int>(scriptVariables.size()); ++index) {
                const EditorSceneContext::EntityScriptVariable& variable = scriptVariables[static_cast<std::size_t>(index)];
                const std::string label = (variable.overridden ? std::string{ "\xE2\x80\xA2 " } : std::string{}) + variable.name;
                if (variable.type == kb::script::ScriptValueType::Bool) {
                    section.Bool(label, variable.value.AsBool(), InspectorPropertyId::ScriptVariable, index);
                } else {
                    // The row index keeps hover + the inline editor buffer in only the
                    // variable being edited (all rows share the ScriptVariable id).
                    section.Field(label, FormatScriptVariableValue(variable.value, variable.type), InspectorPropertyId::ScriptVariable, index);
                }
            }
        }
        y += h + kSectionGap;
    }
    if (const kb::scene::LightComponent* light = scene.Components().Lights().TryGet(selected); light != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::Light, LightSectionRows(*light));
        if (sectionVisible(y, h)) {
            PaintLightSection(dc, content, y, theme, inspector, *light);
        } else {
            y += h + kSectionGap;
        }
    }
    if (const kb::scene::MeshRendererComponent* meshRenderer = scene.Components().MeshRenderers().TryGet(selected); meshRenderer != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::MeshRenderer, MeshRendererRowCount(sceneContext, *meshRenderer));
        if (sectionVisible(y, h)) {
            PaintMeshRendererSection(dc, content, y, theme, inspector, sceneContext, *meshRenderer);
        } else {
            y += h + kSectionGap;
        }
    }
    if (const kb::scene::AudioSourceComponent* audioSource = scene.Components().AudioSources().TryGet(selected); audioSource != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::AudioSource, 10);
        if (sectionVisible(y, h)) {
            PaintAudioSourceSection(dc, content, y, theme, inspector, sceneContext, *audioSource);
        } else {
            y += h + kSectionGap;
        }
    }
    if (const kb::scene::AudioListenerComponent* audioListener = scene.Components().AudioListeners().TryGet(selected); audioListener != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::AudioListener, 2);
        if (sectionVisible(y, h)) {
            PaintAudioListenerSection(dc, content, y, theme, inspector, *audioListener);
        } else {
            y += h + kSectionGap;
        }
    }
    if (const kb::scene::RigidbodyComponent* rigidbody = scene.Components().Rigidbodies().TryGet(selected); rigidbody != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::Rigidbody, static_cast<int>(InspectorPhysicsModel::Fields(*rigidbody).size()));
        if (sectionVisible(y, h)) {
            PaintPhysicsSection(dc, content, y, theme, inspector, InspectorSectionId::Rigidbody, "Rigidbody", InspectorPropertyId::RigidbodyField, InspectorPhysicsModel::Fields(*rigidbody), false, false);
        } else {
            y += h + kSectionGap;
        }
    }
    if (const kb::scene::ColliderComponent* collider = scene.Components().Colliders().TryGet(selected); collider != nullptr) {
        int h = SectionHeight(inspector, InspectorSectionId::Collider, static_cast<int>(InspectorPhysicsModel::Fields(*collider).size()));
        if (!inspector.IsCollapsed(InspectorSectionId::Collider)) {
            h += kFieldRowHeight + kDividerHeight; // the "Fit to Mesh" action button
        }
        if (sectionVisible(y, h)) {
            PaintPhysicsSection(dc, content, y, theme, inspector, InspectorSectionId::Collider, "Collider", InspectorPropertyId::ColliderField, InspectorPhysicsModel::Fields(*collider), true, sceneContext.CanFitColliderToMesh(selected));
        } else {
            y += h + kSectionGap;
        }
    }
    if (const kb::scene::CharacterControllerComponent* character = scene.Components().CharacterControllers().TryGet(selected); character != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::CharacterController, static_cast<int>(InspectorPhysicsModel::Fields(*character).size()));
        if (sectionVisible(y, h)) {
            PaintPhysicsSection(dc, content, y, theme, inspector, InspectorSectionId::CharacterController, "Character Controller", InspectorPropertyId::CharacterControllerField, InspectorPhysicsModel::Fields(*character), false, false);
        } else {
            y += h + kSectionGap;
        }
    }
    if (const kb::scene::JointComponent* joint = scene.Components().Joints().TryGet(selected); joint != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::Joint, static_cast<int>(InspectorPhysicsModel::Fields(*joint).size()));
        if (sectionVisible(y, h)) {
            PaintPhysicsSection(dc, content, y, theme, inspector, InspectorSectionId::Joint, "Joint", InspectorPropertyId::JointField, InspectorPhysicsModel::Fields(*joint), false, false);
        } else {
            y += h + kSectionGap;
        }
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
        const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
        if (const EditorMeshThumbnailStats* stats = EditorMeshPreviewCache().StatsFor(manager, metadata); stats != nullptr) {
            rows += 6;
        }
        if (const EditorMeshValidationResult* validation = EditorMeshPreviewCache().ValidationFor(manager, metadata); validation != nullptr) {
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
    if (IsMaterialDocument(metadata)) {
        height += MaterialPreviewSectionHeight(inspector, MaterialPreviewTelemetryFor(sceneContext, metadata), MaterialDebugChannelRowsFor(sceneContext, metadata).size()) + kSectionGap;
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
    if (EditorMeshPreviewCache().StatsFor(sceneContext.Scene().Assets().Manager(), metadata) != nullptr) {
        height += MeshPreviewPanelHeight(content) + kSectionGap;
    }
    height += SectionHeight(inspector, InspectorSectionId::Asset, AssetSectionRows(sceneContext, metadata));
    return height;
}

// Painted Mesh Renderer rows: Mesh + Material (2) + 6 per material slot +
// Casts/Receives Shadow (2). Must match PaintMeshRendererSection exactly, or the
// scrollable height drifts from the layout and the tail of the panel becomes
// unreachable. The mesh comes from the AssetManager cache (see LoadMeshAssetData).
[[nodiscard]] int MeshRendererRowCount(const EditorSceneContext& sceneContext, const kb::scene::MeshRendererComponent& renderer) {
    const std::optional<kb::render::RenderMeshAssetData> mesh = LoadMeshAssetData(sceneContext, renderer.meshAssetId);
    const std::uint32_t slotCount = InspectorMeshRendererMaterialSlotModel::SlotRowCount(renderer, mesh);
    return 4 + 6 * static_cast<int>(slotCount);
}

[[nodiscard]] int EntityContentHeight(const EditorSceneContext& sceneContext, kb::scene::SceneEntity selected) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const kb::scene::Scene& scene = sceneContext.Scene();
    int height = kHeaderHeight + kPanelPadTop;
    height += SectionHeight(inspector, InspectorSectionId::General, 2) + kSectionGap;
    height += SectionHeight(inspector, InspectorSectionId::Transform, 3) + kSectionGap;
    if (sceneContext.HasEntityScript(selected)) {
        const int scriptRows = 2 + static_cast<int>(sceneContext.EntityScriptExposedVariables(selected).size());
        height += SectionHeight(inspector, InspectorSectionId::Script, scriptRows) + kSectionGap;
    }
    if (const kb::scene::LightComponent* light = scene.Components().Lights().TryGet(selected); light != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::Light, LightSectionRows(*light)) + kSectionGap;
    }
    if (const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(selected); renderer != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::MeshRenderer, MeshRendererRowCount(sceneContext, *renderer)) + kSectionGap;
    }
    if (scene.Components().AudioSources().TryGet(selected) != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::AudioSource, 10) + kSectionGap;
    }
    if (scene.Components().AudioListeners().TryGet(selected) != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::AudioListener, 2) + kSectionGap;
    }
    if (const kb::scene::RigidbodyComponent* rigidbody = scene.Components().Rigidbodies().TryGet(selected); rigidbody != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::Rigidbody, static_cast<int>(InspectorPhysicsModel::Fields(*rigidbody).size())) + kSectionGap;
    }
    if (const kb::scene::ColliderComponent* collider = scene.Components().Colliders().TryGet(selected); collider != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::Collider, static_cast<int>(InspectorPhysicsModel::Fields(*collider).size())) + kSectionGap;
        if (!inspector.IsCollapsed(InspectorSectionId::Collider)) {
            height += kFieldRowHeight + kDividerHeight; // the "Fit to Mesh" action button
        }
    }
    if (const kb::scene::CharacterControllerComponent* character = scene.Components().CharacterControllers().TryGet(selected); character != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::CharacterController, static_cast<int>(InspectorPhysicsModel::Fields(*character).size())) + kSectionGap;
    }
    if (const kb::scene::JointComponent* joint = scene.Components().Joints().TryGet(selected); joint != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::Joint, static_cast<int>(InspectorPhysicsModel::Fields(*joint).size())) + kSectionGap;
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

[[nodiscard]] InspectorPanelRenderer::Hit HitAssetFieldRow(
    RECT row,
    InspectorSectionId section,
    InspectorPropertyId property,
    InspectorPropertyId buttonProperty,
    int x,
    int y) noexcept {
    const RECT value = ValueRectForRow(row);
    const RECT button = AssetPickerButtonRect(value);
    if (Contains(button, x, y)) {
        return MakeHit(InspectorHitKind::TextField, section, buttonProperty, button);
    }
    const RECT text = AssetPickerTextRect(value);
    if (Contains(text, x, y)) {
        return MakeHit(InspectorHitKind::TextField, section, property, text);
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

// Hit-tests a physics component section (header "×" + one row per field). Mirrors
// the render order in PaintPhysicsSection: Bool fields are checkboxes, everything
// else (Float, Enum) is a text row. The hit carries the field's row index.
[[nodiscard]] InspectorPanelRenderer::Hit HitPhysicsSection(
    const RECT& content,
    const InspectorPanelState& state,
    InspectorSectionId section,
    InspectorPropertyId fieldProperty,
    const std::vector<PhysicsField>& fields,
    int x,
    int yPoint,
    int& y,
    bool withFitButton = false) {
    if (InspectorPanelRenderer::Hit hit = HitSectionHeader(content, y, state, section, x, yPoint, true); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (state.IsCollapsed(section)) {
        return {};
    }
    for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
        const RECT row = RowRect(content, y);
        InspectorPanelRenderer::Hit hit = fields[static_cast<std::size_t>(i)].kind == PhysicsFieldKind::Bool
            ? HitBool(row, section, fieldProperty, x, yPoint)
            : HitTextRow(row, section, fieldProperty, x, yPoint);
        if (hit.kind != InspectorHitKind::None) {
            hit.index = i;
            return hit;
        }
        AdvanceRow(y);
    }
    if (withFitButton) {
        const RECT button = ColliderFitButtonRect(RowRect(content, y));
        if (Contains(button, x, yPoint)) {
            return MakeHit(InspectorHitKind::TextField, section, InspectorPropertyId::ColliderFitToMesh, button);
        }
        AdvanceRow(y);
    }
    return {};
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

[[nodiscard]] InspectorPanelRenderer::Hit HitTestMaterialPreviewSection(
    const RECT& content,
    const InspectorPanelState& state,
    const EditorSceneContext& sceneContext,
    const kb::assets::AssetMetadata& metadata,
    int x,
    int yPoint,
    int& y) {
    if (InspectorPanelRenderer::Hit hit = HitSectionHeader(content, y, state, InspectorSectionId::MaterialPreview, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (!state.IsCollapsed(InspectorSectionId::MaterialPreview)) {
        const EditorMaterialPreviewTelemetry telemetry = MaterialPreviewTelemetryFor(sceneContext, metadata);
        y += MaterialPreviewBodyHeight(telemetry, MaterialDebugChannelRowsFor(sceneContext, metadata).size()) - kDividerHeight;
    }
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

[[nodiscard]] InspectorPanelRenderer::Hit HitLightFloatRow(
    const RECT& content,
    int& y,
    InspectorPropertyId property,
    int x,
    int yPoint) noexcept {
    if (InspectorPanelRenderer::Hit hit = HitFloatRow(RowRect(content, y), InspectorSectionId::Light, property, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    AdvanceRow(y);
    return {};
}

[[nodiscard]] InspectorPanelRenderer::Hit HitTestLightSection(
    const RECT& content,
    const InspectorPanelState& state,
    const kb::scene::LightComponent& light,
    int x,
    int yPoint,
    int& y) noexcept {
    if (InspectorPanelRenderer::Hit hit = HitSectionHeader(content, y, state, InspectorSectionId::Light, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (state.IsCollapsed(InspectorSectionId::Light)) {
        return {};
    }

    if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(content, y), InspectorSectionId::Light, InspectorPropertyId::LightKind, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    AdvanceRow(y);
    if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightColorR, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightColorG, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightColorB, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightIntensity, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (LightUsesRange(light.kind)) {
        if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightRange, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
    }
    if (LightUsesSpotCone(light.kind)) {
        if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightInnerCone, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightOuterCone, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
    }
    if (LightUsesAreaSize(light.kind)) {
        if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightAreaWidth, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightAreaHeight, x, yPoint); hit.kind != InspectorHitKind::None) {
            return hit;
        }
    }
    if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightContactShadowLength, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightVolumetricScattering, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(content, y), InspectorSectionId::Light, InspectorPropertyId::LightCastsShadow, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    AdvanceRow(y);
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

// The open Add Component menu's frame in window (event) space. Derived from the
// total content height (the menu is the last thing laid out) rather than by
// re-walking the sections, then shifted by the inspector's own scroll.
static std::optional<RECT> AddComponentBrowserWindowRect(const RECT& content, const EditorSceneContext& sceneContext) {
    const InspectorPanelState& state = sceneContext.Inspector();
    if (!state.IsAddComponentBrowserOpen()) {
        return std::nullopt;
    }
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (sceneContext.AssetBrowser().InspectorAsset().IsValid() ||
        !sceneContext.Scene().Entities().IsAlive(selected) ||
        sceneContext.SelectedHierarchyEntities().size() > 1U) {
        return std::nullopt; // the menu only appears on a single live entity selection
    }
    const int total = EntityContentHeight(sceneContext, selected);
    const int maxInspectorScroll = InspectorPanelRenderer::MaxScrollOffset(content, sceneContext);
    const int inspectorScroll = std::clamp(state.ScrollOffset(), 0, maxInspectorScroll);
    const RECT viewport = ContentViewportRect(content, maxInspectorScroll > 0);
    const int buttonY = content.top - inspectorScroll + total - kAddComponentButtonHeight - (8 + kAddComponentBrowserMaxHeight);
    return AddComponentBrowserRect(viewport, buttonY);
}

InspectorPanelRenderer::AddComponentScrollInfo InspectorPanelRenderer::AddComponentScrollGeometry(const RECT& content, const EditorSceneContext& sceneContext) {
    const std::optional<RECT> browser = AddComponentBrowserWindowRect(content, sceneContext);
    if (!browser.has_value()) {
        return {};
    }
    const InspectorPanelState& state = sceneContext.Inspector();
    const std::string_view query = AddComponentQuery(state);
    const std::string& category = state.AddComponentBrowserCategory();
    const bool showBack = query.empty() && !category.empty();
    const std::vector<AddComponentRow> rows = InspectorAddComponentBrowserModel::Rows(query.empty() ? std::string_view{ category } : std::string_view{}, query);
    const RECT list = AddComponentListRect(*browser, showBack);
    const int listHeight = static_cast<int>(list.bottom - list.top);
    const int rowCount = static_cast<int>(rows.size());
    if (InspectorAddComponentBrowserModel::TotalHeight(rowCount, kAddComponentRowHeight) <= listHeight) {
        return {};
    }
    const int maxScroll = InspectorAddComponentBrowserModel::MaxScroll(rowCount, kAddComponentRowHeight, listHeight);
    const int scroll = std::clamp(state.AddComponentScroll(), 0, maxScroll);
    return AddComponentScrollInfo{
        .active = true,
        .track = AddComponentScrollbarTrackRect(list),
        .thumb = AddComponentScrollbarThumbRect(list, rowCount, scroll),
        .maxScroll = maxScroll,
    };
}

bool InspectorPanelRenderer::AddComponentListContains(const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    const std::optional<RECT> browser = AddComponentBrowserWindowRect(content, sceneContext);
    if (!browser.has_value()) {
        return false;
    }
    const InspectorPanelState& state = sceneContext.Inspector();
    const std::string_view query = AddComponentQuery(state);
    const std::string& category = state.AddComponentBrowserCategory();
    const bool showBack = query.empty() && !category.empty();
    const RECT list = AddComponentListRect(*browser, showBack);
    return x >= list.left && x < list.right && y >= list.top && y < list.bottom;
}

std::optional<RECT> InspectorPanelRenderer::MaterialPreviewRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    static_cast<void>(content);
    static_cast<void>(sceneContext);
    return std::nullopt;
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

    PaintEntity(dc, inner, viewport, theme, sceneContext, selected);
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
        if (metadata != nullptr && IsMaterialDocument(*metadata)) {
            if (InspectorPanelRenderer::Hit hit = HitTestMaterialPreviewSection(viewport, state, sceneContext, *metadata, x, scrolledY, y); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            y += kSectionGap;
            if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::Material, x, scrolledY); hit.kind != InspectorHitKind::None) {
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
            if (EditorMeshPreviewCache().StatsFor(sceneContext.Scene().Assets().Manager(), *metadata) != nullptr) {
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
            if (InspectorPanelRenderer::Hit hit = HitAssetFieldRow(RowRect(viewport, y), InspectorSectionId::Script, InspectorPropertyId::ScriptName, InspectorPropertyId::ScriptPicker, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::Script, InspectorPropertyId::ScriptEnabled, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
            // One row per exposed variable, in the same order the renderer draws
            // them; the row index identifies which variable was hit.
            const std::vector<EditorSceneContext::EntityScriptVariable> scriptVariables = sceneContext.EntityScriptExposedVariables(selected);
            for (int variableIndex = 0; variableIndex < static_cast<int>(scriptVariables.size()); ++variableIndex) {
                InspectorPanelRenderer::Hit hit = scriptVariables[static_cast<std::size_t>(variableIndex)].type == kb::script::ScriptValueType::Bool
                    ? HitBool(RowRect(viewport, y), InspectorSectionId::Script, InspectorPropertyId::ScriptVariable, x, scrolledY)
                    : HitTextRow(RowRect(viewport, y), InspectorSectionId::Script, InspectorPropertyId::ScriptVariable, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) {
                    hit.index = variableIndex;
                    return hit;
                }
                AdvanceRow(y);
            }
        }
        y += kSectionGap;
    }

    if (const kb::scene::LightComponent* light = sceneContext.Scene().Components().Lights().TryGet(selected); light != nullptr) {
        if (InspectorPanelRenderer::Hit hit = HitTestLightSection(viewport, state, *light, x, scrolledY, y); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        y += kSectionGap;
    }

    if (const kb::scene::MeshRendererComponent* renderer = sceneContext.Scene().Components().MeshRenderers().TryGet(selected); renderer != nullptr) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::MeshRenderer, x, scrolledY, true); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (!state.IsCollapsed(InspectorSectionId::MeshRenderer)) {
            if (InspectorPanelRenderer::Hit hit = HitAssetFieldRow(RowRect(viewport, y), InspectorSectionId::MeshRenderer, InspectorPropertyId::MeshRendererMesh, InspectorPropertyId::MeshRendererMeshPicker, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitAssetFieldRow(RowRect(viewport, y), InspectorSectionId::MeshRenderer, InspectorPropertyId::MeshRendererMaterial, InspectorPropertyId::MeshRendererMaterialPicker, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
            const std::optional<kb::render::RenderMeshAssetData> mesh = LoadMeshAssetData(sceneContext, renderer->meshAssetId);
            const std::vector<InspectorMeshRendererMaterialSlotRow> slotRows = InspectorMeshRendererMaterialSlotModel::Build(
                *renderer,
                mesh,
                [&sceneContext](std::uint64_t materialId) { return MaterialDisplayName(sceneContext, materialId); });
            for (const InspectorMeshRendererMaterialSlotRow& row : slotRows) {
                AdvanceRow(y);
                AdvanceRow(y);
                AdvanceRow(y);
                if (InspectorPanelRenderer::Hit hit = HitAssetFieldRow(
                        RowRect(viewport, y),
                        InspectorSectionId::MeshRenderer,
                        MeshRendererMaterialSlotProperty(row.slotIndex),
                        MeshRendererMaterialSlotPickerProperty(row.slotIndex),
                        x,
                        scrolledY);
                    hit.kind != InspectorHitKind::None) {
                    return hit;
                }
                AdvanceRow(y);
                AdvanceRow(y);
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

    if (const kb::scene::RigidbodyComponent* rigidbody = sceneContext.Scene().Components().Rigidbodies().TryGet(selected); rigidbody != nullptr) {
        if (InspectorPanelRenderer::Hit hit = HitPhysicsSection(viewport, state, InspectorSectionId::Rigidbody, InspectorPropertyId::RigidbodyField, InspectorPhysicsModel::Fields(*rigidbody), x, scrolledY, y); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        y += kSectionGap;
    }
    if (const kb::scene::ColliderComponent* collider = sceneContext.Scene().Components().Colliders().TryGet(selected); collider != nullptr) {
        if (InspectorPanelRenderer::Hit hit = HitPhysicsSection(viewport, state, InspectorSectionId::Collider, InspectorPropertyId::ColliderField, InspectorPhysicsModel::Fields(*collider), x, scrolledY, y, true); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        y += kSectionGap;
    }
    if (const kb::scene::CharacterControllerComponent* character = sceneContext.Scene().Components().CharacterControllers().TryGet(selected); character != nullptr) {
        if (InspectorPanelRenderer::Hit hit = HitPhysicsSection(viewport, state, InspectorSectionId::CharacterController, InspectorPropertyId::CharacterControllerField, InspectorPhysicsModel::Fields(*character), x, scrolledY, y); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        y += kSectionGap;
    }
    if (const kb::scene::JointComponent* joint = sceneContext.Scene().Components().Joints().TryGet(selected); joint != nullptr) {
        if (InspectorPanelRenderer::Hit hit = HitPhysicsSection(viewport, state, InspectorSectionId::Joint, InspectorPropertyId::JointField, InspectorPhysicsModel::Fields(*joint), x, scrolledY, y); hit.kind != InspectorHitKind::None) {
            return hit;
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
        const std::string_view query = AddComponentQuery(state);
        const std::string& category = state.AddComponentBrowserCategory();
        const bool showBack = query.empty() && !category.empty();
        if (showBack) {
            const RECT back = AddComponentBackHeaderRect(browser);
            if (Contains(back, x, scrolledY)) {
                return MakeHit(InspectorHitKind::TextField, InspectorSectionId::AddComponent, InspectorPropertyId::AddComponentBack, back);
            }
        }
        const std::vector<AddComponentRow> rows = InspectorAddComponentBrowserModel::Rows(query.empty() ? std::string_view{ category } : std::string_view{}, query);
        const RECT list = AddComponentListRect(browser, showBack);
        const int listHeight = static_cast<int>(list.bottom - list.top);
        const int rowCount = static_cast<int>(rows.size());
        const bool browserScrollable = InspectorAddComponentBrowserModel::TotalHeight(rowCount, kAddComponentRowHeight) > listHeight;
        const int browserScroll = std::clamp(state.AddComponentScroll(), 0, InspectorAddComponentBrowserModel::MaxScroll(rowCount, kAddComponentRowHeight, listHeight));
        if (browserScrollable) {
            const RECT thumb = AddComponentScrollbarThumbRect(list, rowCount, browserScroll);
            if (thumb.bottom > thumb.top && Contains(thumb, x, scrolledY)) {
                return MakeHit(InspectorHitKind::ScrollbarThumb, InspectorSectionId::AddComponent, InspectorPropertyId::None, thumb);
            }
            const RECT track = AddComponentScrollbarTrackRect(list);
            if (Contains(track, x, scrolledY)) {
                return MakeHit(InspectorHitKind::ScrollbarTrack, InspectorSectionId::AddComponent, InspectorPropertyId::None, track);
            }
        }
        const RECT inner = AddComponentListInnerRect(list, browserScrollable);
        if (scrolledY >= list.top && scrolledY < list.bottom && x >= inner.left && x < inner.right) {
            const InspectorAddComponentBrowserModel::VisibleWindow window = InspectorAddComponentBrowserModel::Visible(rowCount, browserScroll, kAddComponentRowHeight, listHeight);
            for (int i = window.first; i < window.first + window.count; ++i) {
                const int rowTop = list.top - browserScroll + i * kAddComponentRowHeight;
                const RECT rowRect = Rect(inner.left, rowTop, inner.right, rowTop + kAddComponentRowHeight);
                if (Contains(rowRect, x, scrolledY)) {
                    InspectorPanelRenderer::Hit hit = MakeHit(InspectorHitKind::TextField, InspectorSectionId::AddComponent, InspectorPropertyId::AddComponentOption, rowRect);
                    hit.index = i;
                    return hit;
                }
            }
        }
    }
    return {};
}

} // namespace kb::editor

#endif
