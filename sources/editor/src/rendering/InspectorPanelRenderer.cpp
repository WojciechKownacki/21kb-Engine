#include "rendering/InspectorPanelRenderer.hpp"

#include "rendering/MaterialPreviewAppearanceResolver.hpp"
#include "rendering/MaterialPreviewTextureAverageColor.hpp"
#include "rendering/InspectorPanelSectionRows.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/GuideCurveComponent.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"
#include "engine/scene/RegionPortalComponent.hpp"
#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/GeometrySwarmComponent.hpp"
#include "engine/scene/SurfaceCastComponent.hpp"
#include "engine/scene/FacingPanelComponent.hpp"
#include "engine/scene/SpaceStrokeComponent.hpp"
#include "engine/scene/HistoryRibbonComponent.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
#include "engine/scene/TagsComponent.hpp"
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
using inspector_panel_rows::DisplayField;
using inspector_panel_rows::kDisclosureRowHeight;
using inspector_panel_rows::kDisclosureTextOffset;
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

constexpr std::array<InspectorRowDefinition, 4> kAnimatorRows{ {
    { InspectorPropertyId::AnimatorController, InspectorRowValueKind::Text },
    { InspectorPropertyId::AnimatorSpeed, InspectorRowValueKind::Text },
    { InspectorPropertyId::AnimatorEnabled, InspectorRowValueKind::Bool },
    { InspectorPropertyId::AnimatorRootMotionOwner, InspectorRowValueKind::Text },
} };

constexpr std::array<InspectorRowDefinition, 2> kUIDocumentRows{ {
    { InspectorPropertyId::UIDocumentAsset, InspectorRowValueKind::Text },
    { InspectorPropertyId::UIDocumentEnabled, InspectorRowValueKind::Bool },
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

void DrawAddComponentBrowser(HDC dc, RECT browser, const EditorTheme& theme, const InspectorPanelState& inspector) {
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

[[nodiscard]] InspectorMaterialPreviewStyle MaterialPreviewStyleFor(
    const std::optional<kb::render::RenderMaterialAssetData>& material,
    const kb::assets::AssetManager* assets) {
    InspectorMaterialPreviewStyle style{};
    if (!material.has_value()) {
        return style;
    }
    // Graph-backed materials leave desc at its white fallbacks, so read what the graph feeds into
    // Material Output instead of painting every graph material as a white ball.
    const MaterialPreviewAppearance appearance = MaterialPreviewAppearanceResolver::Resolve(*material, assets, &MaterialPreviewTextureAverageColor);
    style.baseColor = ToColorRef(appearance.baseColor[0], appearance.baseColor[1], appearance.baseColor[2]);
    style.emissiveColor = ToColorRef(appearance.emissiveColor[0], appearance.emissiveColor[1], appearance.emissiveColor[2]);
    style.roughness = appearance.roughness;
    style.metallic = appearance.metallic;
    style.emissiveStrength = appearance.emissiveStrength;
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

// Paint and the live 3D surface must agree on one rect, so the geometry lives here and both use it.
// Preview is the first section of a material asset, so its position follows from the header alone.
[[nodiscard]] RECT MaterialPreviewFrameRect(const RECT& content) noexcept {
    const int y = content.top + kHeaderHeight + kPanelPadTop + kSectionHeaderHeight + kDividerHeight;
    return Rect(
        content.left + kMaterialPreviewPadding,
        y + kMaterialPreviewGap,
        content.right - kMaterialPreviewPadding,
        y + kMaterialPreviewGap + kMaterialPreviewHeight);
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
    const RECT frame = MaterialPreviewFrameRect(content);
    DrawFrame(dc, frame, Rgb(13, 15, 18), Color(theme.borderPanel));
    // The real material renders here: the same preview scene, lighting and post-process the Material
    // Editor uses, presented into this rect. The software-shaded ball is only the fallback for when that
    // scene is not available, because it can never show textures or the actual lighting response.
    if (!telemetry.previewSceneReady) {
        DrawStaticMaterialPreview(dc, frame, MaterialPreviewStyleFor(material, &sceneContext.Scene().Assets().Manager()));
    }
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
    DrawTelemetryRow(dc, content, y, theme, inspector, "Virtual Path", NormalizePath(metadata.virtualPath));
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
    const std::string headerLabel = std::string{ MaterialDocumentLabel(metadata) } + (sceneContext.HasDirtyMaterialAssetEdit() ? " *" : "");
    DrawHeader(dc, content, theme, HeroIconKind::Cube, metadata.name.empty() ? metadata.virtualPath.filename().string() : metadata.name, headerLabel);
    // A material is authored in the Material Editor, so its Inspector is the preview and nothing else:
    // the old Material and Asset sections only restated what the graph already owns.
    static_cast<void>(DrawMaterialPreview(dc, content, content.top + kHeaderHeight + kPanelPadTop, theme, inspector, sceneContext, metadata));
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

void PaintAnimatorSection(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const EditorSceneContext& sceneContext,
    const kb::scene::Animator& animator) {
    const char* rootMotionOwner = "Invalid";
    switch (animator.rootMotionOwner) {
    case kb::scene::AnimatorRootMotionOwner::None: rootMotionOwner = "None"; break;
    case kb::scene::AnimatorRootMotionOwner::Animator: rootMotionOwner = "Animator"; break;
    case kb::scene::AnimatorRootMotionOwner::CharacterController: rootMotionOwner = "Character Controller"; break;
    case kb::scene::AnimatorRootMotionOwner::Rigidbody: rootMotionOwner = "Rigidbody"; break;
    default: break;
    }
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector,
        InspectorSectionId::Animator, HeroIconKind::Play, "Animator");
    section.Field("Controller", AssetDisplayName(sceneContext, animator.controllerAssetId), InspectorPropertyId::AnimatorController);
    section.Field("Speed", FormatFloat(animator.speed, 3), InspectorPropertyId::AnimatorSpeed);
    section.Bool("Enabled", animator.enabled, InspectorPropertyId::AnimatorEnabled);
    section.Field("Root Motion", rootMotionOwner, InspectorPropertyId::AnimatorRootMotionOwner);
    y = section.Bottom() + kSectionGap;
}

void PaintUIDocumentSection(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const EditorSceneContext& sceneContext,
    const kb::scene::UIDocumentComponent& document) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector,
        InspectorSectionId::UIDocument, HeroIconKind::DocumentText, "UI Document");
    section.Field("Document", AssetDisplayName(sceneContext, document.documentAssetId), InspectorPropertyId::UIDocumentAsset);
    section.Bool("Enabled", document.enabled, InspectorPropertyId::UIDocumentEnabled);
    y = section.Bottom() + kSectionGap;
}

void PaintNavAgentSection(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const kb::scene::NavAgent& agent) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector,
        InspectorSectionId::NavAgent, HeroIconKind::Gamepad2, "Nav Agent", true);
    section.Field("Radius", FormatFloat(agent.radius, 3), InspectorPropertyId::NavAgentRadius);
    section.Field("Height", FormatFloat(agent.height, 3), InspectorPropertyId::NavAgentHeight);
    section.Field("Max Speed", FormatFloat(agent.maxSpeed, 3), InspectorPropertyId::NavAgentMaxSpeed);
    section.Field("Acceleration", FormatFloat(agent.acceleration, 3), InspectorPropertyId::NavAgentAcceleration);
    section.Field("Angular Speed", FormatFloat(agent.angularSpeedDegrees, 3), InspectorPropertyId::NavAgentAngularSpeed);
    section.Field("Stopping Distance", FormatFloat(agent.stoppingDistance, 3), InspectorPropertyId::NavAgentStoppingDistance);
    section.Field("Area Mask", std::to_string(agent.areaMask), InspectorPropertyId::NavAgentAreaMask);
    section.Vec3("Destination", agent.destination, InspectorPropertyId::NavAgentDestinationX, InspectorPropertyId::NavAgentDestinationY, InspectorPropertyId::NavAgentDestinationZ);
    section.Bool("Enabled", agent.enabled, InspectorPropertyId::NavAgentEnabled);
    y = section.Bottom() + kSectionGap;
}

void PaintNavObstacleSection(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const kb::scene::NavObstacle& obstacle) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector,
        InspectorSectionId::NavObstacle, HeroIconKind::Cube, "Nav Obstacle", true);
    section.Field("Shape", obstacle.shape == kb::scene::NavObstacleShape::Box ? "Box" : "Cylinder", InspectorPropertyId::NavObstacleShape);
    section.Vec3("Center", obstacle.center, InspectorPropertyId::NavObstacleCenterX, InspectorPropertyId::NavObstacleCenterY, InspectorPropertyId::NavObstacleCenterZ);
    section.Vec3("Size", obstacle.size, InspectorPropertyId::NavObstacleSizeX, InspectorPropertyId::NavObstacleSizeY, InspectorPropertyId::NavObstacleSizeZ);
    section.Field("Radius", FormatFloat(obstacle.radius, 3), InspectorPropertyId::NavObstacleRadius);
    section.Field("Height", FormatFloat(obstacle.height, 3), InspectorPropertyId::NavObstacleHeight);
    section.Field("Area", std::to_string(obstacle.area), InspectorPropertyId::NavObstacleArea);
    section.Bool("Carve", obstacle.carve, InspectorPropertyId::NavObstacleCarve);
    section.Bool("Enabled", obstacle.enabled, InspectorPropertyId::NavObstacleEnabled);
    y = section.Bottom() + kSectionGap;
}

[[nodiscard]] const char* RegionShapeKindLabel(kb::scene::RegionShapeKind kind) noexcept {
    switch (kind) {
    case kb::scene::RegionShapeKind::Circle2D: return "Circle 2D";
    case kb::scene::RegionShapeKind::Rectangle2D: return "Rectangle 2D";
    case kb::scene::RegionShapeKind::Sphere: return "Sphere";
    case kb::scene::RegionShapeKind::Box: return "Box";
    case kb::scene::RegionShapeKind::Capsule: return "Capsule";
    }
    return "Invalid";
}

void PaintRegionShapeSection(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const kb::scene::RegionShapeComponent& shape) {
    SectionWriter section(
        dc,
        Rect(content.left, y, content.right, content.bottom),
        theme,
        inspector,
        InspectorSectionId::RegionShape,
        HeroIconKind::Cube,
        "Region Shape",
        true);
    section.Field("Shape", RegionShapeKindLabel(shape.kind), InspectorPropertyId::RegionShapeKind);
    section.Vec3("Center", shape.center, InspectorPropertyId::RegionShapeCenterX, InspectorPropertyId::RegionShapeCenterY, InspectorPropertyId::RegionShapeCenterZ);
    section.Vec3("Size", shape.size, InspectorPropertyId::RegionShapeSizeX, InspectorPropertyId::RegionShapeSizeY, InspectorPropertyId::RegionShapeSizeZ);
    section.Field("Radius", FormatFloat(shape.radius, 3), InspectorPropertyId::RegionShapeRadius);
    section.Field("Height", FormatFloat(shape.height, 3), InspectorPropertyId::RegionShapeHeight);
    section.Bool("Enabled", shape.enabled, InspectorPropertyId::RegionShapeEnabled);
    y = section.Bottom() + kSectionGap;
}

void PaintGuideCurveSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::GuideCurveComponent& curve) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::GuideCurve, HeroIconKind::Cube, "Guide Curve", true);
    section.Field("Control Points", std::to_string(curve.controlPointCount), InspectorPropertyId::GuideCurveControlPointCount);
    section.Field("Interpolation", curve.interpolation == kb::scene::GuideCurveInterpolation::Linear ? "Linear" : "Catmull-Rom", InspectorPropertyId::GuideCurveInterpolation);
    section.Bool("Closed", curve.closed, InspectorPropertyId::GuideCurveClosed);
    section.Bool("Enabled", curve.enabled, InspectorPropertyId::GuideCurveEnabled);
    y = section.Bottom() + kSectionGap;
}

void PaintContentInstanceSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::ContentInstanceComponent& instance) {
    const char* kind = instance.kind == kb::scene::ContentInstanceKind::Prefab ? "Prefab" : instance.kind == kb::scene::ContentInstanceKind::Subscene ? "Subscene" : "World Fragment";
    const char* lifetime = instance.lifetime == kb::scene::ContentInstanceLifetime::Owner ? "Owner" : "Persistent";
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::ContentInstance, HeroIconKind::Cube, "Content Instance", true);
    section.Field("Asset ID", std::to_string(instance.assetId), InspectorPropertyId::ContentInstanceAssetId);
    section.Field("Source Type", kind, InspectorPropertyId::ContentInstanceKind);
    section.Field("Lifetime", lifetime, InspectorPropertyId::ContentInstanceLifetime);
    section.Bool("Active", instance.active, InspectorPropertyId::ContentInstanceActive);
    y = section.Bottom() + kSectionGap;
}

void PaintStreamFocusSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::StreamFocusComponent& focus) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::StreamFocus, HeroIconKind::Cube, "Stream Focus", true);
    section.Field("Inner Radius", FormatFloat(focus.innerRadius, 3), InspectorPropertyId::StreamFocusInnerRadius);
    section.Field("Outer Radius", FormatFloat(focus.outerRadius, 3), InspectorPropertyId::StreamFocusOuterRadius);
    section.Field("Priority", std::to_string(focus.priority), InspectorPropertyId::StreamFocusPriority);
    section.Field("Load Mask", std::to_string(static_cast<std::uint32_t>(focus.loadMask)), InspectorPropertyId::StreamFocusLoadMask);
    section.Bool("Enabled", focus.enabled, InspectorPropertyId::StreamFocusEnabled);
    y = section.Bottom() + kSectionGap;
}

[[nodiscard]] const char* WorldBackdropModeName(kb::scene::WorldBackdropMode mode) noexcept {
    switch (mode) {
    case kb::scene::WorldBackdropMode::SolidColor: return "Solid Color";
    case kb::scene::WorldBackdropMode::VerticalGradient: return "Vertical Gradient";
    case kb::scene::WorldBackdropMode::EnvironmentMap: return "Environment Map (2D Equirectangular)";
    case kb::scene::WorldBackdropMode::ProceduralSky: return "Procedural Sky";
    }
    return "Invalid";
}

void PaintWorldBackdropSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::WorldBackdropComponent& backdrop) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::WorldBackdrop, HeroIconKind::Eye, "World Backdrop", true);
    section.Field("Mode", WorldBackdropModeName(backdrop.mode), InspectorPropertyId::WorldBackdropMode);
    section.Field("Color R", FormatFloat(backdrop.color.x, 3), InspectorPropertyId::WorldBackdropColorR);
    section.Field("Color G", FormatFloat(backdrop.color.y, 3), InspectorPropertyId::WorldBackdropColorG);
    section.Field("Color B", FormatFloat(backdrop.color.z, 3), InspectorPropertyId::WorldBackdropColorB);
    section.Field("Horizon R", FormatFloat(backdrop.horizonColor.x, 3), InspectorPropertyId::WorldBackdropHorizonColorR);
    section.Field("Horizon G", FormatFloat(backdrop.horizonColor.y, 3), InspectorPropertyId::WorldBackdropHorizonColorG);
    section.Field("Horizon B", FormatFloat(backdrop.horizonColor.z, 3), InspectorPropertyId::WorldBackdropHorizonColorB);
    section.Field("Zenith R", FormatFloat(backdrop.zenithColor.x, 3), InspectorPropertyId::WorldBackdropZenithColorR);
    section.Field("Zenith G", FormatFloat(backdrop.zenithColor.y, 3), InspectorPropertyId::WorldBackdropZenithColorG);
    section.Field("Zenith B", FormatFloat(backdrop.zenithColor.z, 3), InspectorPropertyId::WorldBackdropZenithColorB);
    section.Field("Environment Asset", std::to_string(backdrop.environmentAssetId), InspectorPropertyId::WorldBackdropEnvironmentAssetId);
    section.Field("Horizon Height", FormatFloat(backdrop.horizonHeight, 3), InspectorPropertyId::WorldBackdropHorizonHeight);
    section.Field("Gradient Exponent", FormatFloat(backdrop.gradientExponent, 3), InspectorPropertyId::WorldBackdropGradientExponent);
    section.Field("Priority", std::to_string(backdrop.priority), InspectorPropertyId::WorldBackdropPriority);
    section.Bool("Enabled", backdrop.enabled, InspectorPropertyId::WorldBackdropEnabled);
    y = section.Bottom() + kSectionGap;
}

[[nodiscard]] const char* AmbientRadianceModeName(kb::scene::AmbientRadianceMode mode) noexcept {
    switch (mode) {
    case kb::scene::AmbientRadianceMode::Constant: return "Constant";
    case kb::scene::AmbientRadianceMode::Gradient: return "Gradient";
    case kb::scene::AmbientRadianceMode::EnvironmentMap: return "Environment Map (2D Equirectangular)";
    case kb::scene::AmbientRadianceMode::ProceduralSky: return "Procedural Sky";
    case kb::scene::AmbientRadianceMode::CapturedEnvironment: return "Captured Environment";
    case kb::scene::AmbientRadianceMode::EstimatedEnvironment: return "Estimated Environment";
    }
    return "Invalid";
}

void PaintAmbientRadianceSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::AmbientRadianceComponent& ambient) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::AmbientRadiance, HeroIconKind::Bolt, "Ambient Radiance", true);
    section.Field("Mode", AmbientRadianceModeName(ambient.mode), InspectorPropertyId::AmbientRadianceMode);
    section.Field("Color R", FormatFloat(ambient.color.x, 3), InspectorPropertyId::AmbientRadianceColorR);
    section.Field("Color G", FormatFloat(ambient.color.y, 3), InspectorPropertyId::AmbientRadianceColorG);
    section.Field("Color B", FormatFloat(ambient.color.z, 3), InspectorPropertyId::AmbientRadianceColorB);
    section.Field("Horizon R", FormatFloat(ambient.horizonColor.x, 3), InspectorPropertyId::AmbientRadianceHorizonColorR);
    section.Field("Horizon G", FormatFloat(ambient.horizonColor.y, 3), InspectorPropertyId::AmbientRadianceHorizonColorG);
    section.Field("Horizon B", FormatFloat(ambient.horizonColor.z, 3), InspectorPropertyId::AmbientRadianceHorizonColorB);
    section.Field("Zenith R", FormatFloat(ambient.zenithColor.x, 3), InspectorPropertyId::AmbientRadianceZenithColorR);
    section.Field("Zenith G", FormatFloat(ambient.zenithColor.y, 3), InspectorPropertyId::AmbientRadianceZenithColorG);
    section.Field("Zenith B", FormatFloat(ambient.zenithColor.z, 3), InspectorPropertyId::AmbientRadianceZenithColorB);
    section.Field("Environment Asset", std::to_string(ambient.environmentAssetId), InspectorPropertyId::AmbientRadianceEnvironmentAssetId);
    section.Field("Intensity", FormatFloat(ambient.intensity, 3), InspectorPropertyId::AmbientRadianceIntensity);
    section.Field("Diffuse Intensity", FormatFloat(ambient.diffuseIntensity, 3), InspectorPropertyId::AmbientRadianceDiffuseIntensity);
    section.Field("Specular Intensity", FormatFloat(ambient.specularIntensity, 3), InspectorPropertyId::AmbientRadianceSpecularIntensity);
    section.Field("Priority", std::to_string(ambient.priority), InspectorPropertyId::AmbientRadiancePriority);
    section.Bool("Enabled", ambient.enabled, InspectorPropertyId::AmbientRadianceEnabled);
    y = section.Bottom() + kSectionGap;
}

void PaintDetailSwitchSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::SceneDetailSwitchComponent& detailSwitch) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::DetailSwitch, HeroIconKind::Cube, "Detail Switch", true);
    section.Field("Group", std::to_string(detailSwitch.groupId), InspectorPropertyId::DetailSwitchGroupId);
    section.Field("Minimum LOD", std::to_string(detailSwitch.minimumLod), InspectorPropertyId::DetailSwitchMinimumLod);
    section.Field("Maximum LOD", std::to_string(detailSwitch.maximumLod), InspectorPropertyId::DetailSwitchMaximumLod);
    section.Field("Promote Coverage", FormatFloat(detailSwitch.promoteCoverage, 3), InspectorPropertyId::DetailSwitchPromoteCoverage);
    section.Field("Demote Coverage", FormatFloat(detailSwitch.demoteCoverage, 3), InspectorPropertyId::DetailSwitchDemoteCoverage);
    section.Bool("Enabled", detailSwitch.enabled, InspectorPropertyId::DetailSwitchEnabled);
    y = section.Bottom() + kSectionGap;
}

void PaintVisibilityBlockerSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::SceneVisibilityBlockerComponent& blocker) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::VisibilityBlocker, HeroIconKind::Cube, "Visibility Blocker", true);
    section.Field("Center X", FormatFloat(blocker.localCenter.x, 3), InspectorPropertyId::VisibilityBlockerCenterX);
    section.Field("Center Y", FormatFloat(blocker.localCenter.y, 3), InspectorPropertyId::VisibilityBlockerCenterY);
    section.Field("Center Z", FormatFloat(blocker.localCenter.z, 3), InspectorPropertyId::VisibilityBlockerCenterZ);
    section.Field("Size X", FormatFloat(blocker.size.x, 3), InspectorPropertyId::VisibilityBlockerSizeX);
    section.Field("Size Y", FormatFloat(blocker.size.y, 3), InspectorPropertyId::VisibilityBlockerSizeY);
    section.Field("Size Z", FormatFloat(blocker.size.z, 3), InspectorPropertyId::VisibilityBlockerSizeZ);
    section.Bool("Enabled", blocker.enabled, InspectorPropertyId::VisibilityBlockerEnabled);
    y = section.Bottom() + kSectionGap;
}

void PaintVisibilityCellSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::VisibilityCellComponent& cell) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::VisibilityCell, HeroIconKind::Cube, "Visibility Cell", true);
    section.Field("Membership Mask", std::to_string(cell.membershipMask), InspectorPropertyId::VisibilityCellMembershipMask);
    section.Field("Membership", std::to_string(static_cast<int>(cell.membership)), InspectorPropertyId::VisibilityCellMembership);
    section.Field("Visibility Override", std::to_string(static_cast<int>(cell.visibilityOverride)), InspectorPropertyId::VisibilityCellOverride);
    section.Bool("Enabled", cell.enabled, InspectorPropertyId::VisibilityCellEnabled);
    y = section.Bottom() + kSectionGap;
}

void PaintRegionPortalSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::SceneRegionPortalComponent& portal) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::RegionPortal, HeroIconKind::Cube, "Region Portal", true);
    section.Field("Source Cell", std::to_string(portal.sourceCell.Id()), InspectorPropertyId::RegionPortalSourceCell);
    section.Field("Target Cell", std::to_string(portal.targetCell.Id()), InspectorPropertyId::RegionPortalTargetCell);
    section.Field("Purposes", std::to_string(portal.purposes), InspectorPropertyId::RegionPortalPurposes);
    section.Bool("Enabled", portal.enabled, InspectorPropertyId::RegionPortalEnabled);
    y = section.Bottom() + kSectionGap;
}

void PaintSecondaryFrameSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::AuxFrameComponent& frame) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::SecondaryFrame, HeroIconKind::Eye, "Secondary Frame", true);
    const char* modeName = "Flat";
    switch (frame.mode) {
    case kb::scene::AuxFrameMode::Flat: modeName = "Flat"; break;
    case kb::scene::AuxFrameMode::Mirror: modeName = "Mirror"; break;
    case kb::scene::AuxFrameMode::Cube: modeName = "Cube"; break;
    case kb::scene::AuxFrameMode::Panoramic: modeName = "Panoramic 360° Atlas"; break;
    }
    section.Field("Mode", modeName, InspectorPropertyId::SecondaryFrameMode);
    section.Field("Image Target", std::to_string(frame.imageTargetId), InspectorPropertyId::SecondaryFrameImageTargetId);
    section.Field("Width", std::to_string(frame.width), InspectorPropertyId::SecondaryFrameWidth);
    section.Field("Height", std::to_string(frame.height), InspectorPropertyId::SecondaryFrameHeight);
    section.Field("Mirror Plane X", FormatFloat(frame.mirrorPlaneNormal.x, 3), InspectorPropertyId::SecondaryFramePlaneNormalX);
    section.Field("Mirror Plane Y", FormatFloat(frame.mirrorPlaneNormal.y, 3), InspectorPropertyId::SecondaryFramePlaneNormalY);
    section.Field("Mirror Plane Z", FormatFloat(frame.mirrorPlaneNormal.z, 3), InspectorPropertyId::SecondaryFramePlaneNormalZ);
    section.Field("Mirror Offset", FormatFloat(frame.mirrorPlaneOffset, 3), InspectorPropertyId::SecondaryFramePlaneOffset);
    section.Bool("Enabled", frame.enabled, InspectorPropertyId::SecondaryFrameEnabled);
    y = section.Bottom() + kSectionGap;
}

void PaintGeometrySwarmSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::GeometrySwarmComponent& swarm) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::GeometrySwarm, HeroIconKind::Cube, "Geometry Swarm", true);
    section.Field("Mesh", std::to_string(swarm.meshAssetId), InspectorPropertyId::GeometrySwarmMeshAssetId);
    section.Field("Material", std::to_string(swarm.materialAssetId), InspectorPropertyId::GeometrySwarmMaterialAssetId);
    section.Field("Instances", std::to_string(swarm.instanceCount), InspectorPropertyId::GeometrySwarmInstanceCount);
    section.Field("Columns", std::to_string(swarm.columns), InspectorPropertyId::GeometrySwarmColumns);
    section.Field("Rows", std::to_string(swarm.rows), InspectorPropertyId::GeometrySwarmRows);
    section.Field("Layers", std::to_string(swarm.layers), InspectorPropertyId::GeometrySwarmLayers);
    section.Field("Spacing X", FormatFloat(swarm.spacing.x, 3), InspectorPropertyId::GeometrySwarmSpacingX);
    section.Field("Spacing Y", FormatFloat(swarm.spacing.y, 3), InspectorPropertyId::GeometrySwarmSpacingY);
    section.Field("Spacing Z", FormatFloat(swarm.spacing.z, 3), InspectorPropertyId::GeometrySwarmSpacingZ);
    section.Field("Instance Scale", FormatFloat(swarm.instanceScale, 3), InspectorPropertyId::GeometrySwarmInstanceScale);
    section.Field("Render Layer", std::to_string(swarm.layer), InspectorPropertyId::GeometrySwarmLayer);
    section.Bool("Cast Shadows", swarm.castsShadow, InspectorPropertyId::GeometrySwarmCastsShadow);
    section.Bool("Receive Shadows", swarm.receivesShadow, InspectorPropertyId::GeometrySwarmReceivesShadow);
    section.Bool("Enabled", swarm.enabled, InspectorPropertyId::GeometrySwarmEnabled);
    y = section.Bottom() + kSectionGap;
}
void PaintSurfaceCastSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::SurfaceCastComponent& surfaceCast) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::SurfaceCast, HeroIconKind::Cube, "Surface Cast", true);
    section.Field("Material", std::to_string(surfaceCast.materialAssetId), InspectorPropertyId::SurfaceCastMaterialAssetId);
    section.Field("Receiver Layers", std::to_string(surfaceCast.receiverLayerMask), InspectorPropertyId::SurfaceCastReceiverLayerMask);
    section.Field("Order", std::to_string(surfaceCast.order), InspectorPropertyId::SurfaceCastOrder);
    section.Field("Content", surfaceCast.content == kb::scene::SurfaceCastContent::Material ? "Material" : "Detail", InspectorPropertyId::SurfaceCastContent);
    section.Bool("Enabled", surfaceCast.enabled, InspectorPropertyId::SurfaceCastEnabled);
    y = section.Bottom() + kSectionGap;
}
void PaintFacingPanelSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::FacingPanelComponent& panel) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::FacingPanel, HeroIconKind::Eye, "Facing Panel", true);
    constexpr std::array<const char*, 4> modeNames{ "View", "Point", "Axis", "Fixed" };
    section.Field("Mode", modeNames[static_cast<std::size_t>(panel.mode)], InspectorPropertyId::FacingPanelMode);
    section.Field("Target X", FormatFloat(panel.targetPoint.x, 3), InspectorPropertyId::FacingPanelTargetX);
    section.Field("Target Y", FormatFloat(panel.targetPoint.y, 3), InspectorPropertyId::FacingPanelTargetY);
    section.Field("Target Z", FormatFloat(panel.targetPoint.z, 3), InspectorPropertyId::FacingPanelTargetZ);
    section.Field("Axis X", FormatFloat(panel.axis.x, 3), InspectorPropertyId::FacingPanelAxisX);
    section.Field("Axis Y", FormatFloat(panel.axis.y, 3), InspectorPropertyId::FacingPanelAxisY);
    section.Field("Axis Z", FormatFloat(panel.axis.z, 3), InspectorPropertyId::FacingPanelAxisZ);
    section.Field("Up X", FormatFloat(panel.up.x, 3), InspectorPropertyId::FacingPanelUpX);
    section.Field("Up Y", FormatFloat(panel.up.y, 3), InspectorPropertyId::FacingPanelUpY);
    section.Field("Up Z", FormatFloat(panel.up.z, 3), InspectorPropertyId::FacingPanelUpZ);
    section.Bool("Enabled", panel.enabled, InspectorPropertyId::FacingPanelEnabled);
    y = section.Bottom() + kSectionGap;
}
void PaintSpaceStrokeSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::SpaceStrokeComponent& stroke) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::SpaceStroke, HeroIconKind::Cube, "Kreska przestrzenna", true);
    constexpr std::array<const char*, 4> modeNames{ "Polyline", "Spline", "Beam", "Cable" };
    section.Field("Segment Mesh", std::to_string(stroke.meshAssetId), InspectorPropertyId::SpaceStrokeMeshAssetId);
    section.Field("Material", std::to_string(stroke.materialAssetId), InspectorPropertyId::SpaceStrokeMaterialAssetId);
    section.Field("Mode", modeNames[static_cast<std::size_t>(stroke.mode)], InspectorPropertyId::SpaceStrokeMode);
    section.Field("Width", FormatFloat(stroke.width, 3), InspectorPropertyId::SpaceStrokeWidth);
    section.Field("Cable Sag", FormatFloat(stroke.cableSag, 3), InspectorPropertyId::SpaceStrokeCableSag);
    section.Field("Spline Segments", std::to_string(stroke.splineSegments), InspectorPropertyId::SpaceStrokeSplineSegments);
    section.Field("Render Layer", std::to_string(stroke.layer), InspectorPropertyId::SpaceStrokeLayer);
    section.Bool("Cast Shadows", stroke.castsShadow, InspectorPropertyId::SpaceStrokeCastsShadow);
    section.Bool("Receive Shadows", stroke.receivesShadow, InspectorPropertyId::SpaceStrokeReceivesShadow);
    section.Bool("Enabled", stroke.enabled, InspectorPropertyId::SpaceStrokeEnabled);
    y = section.Bottom() + kSectionGap;
}

void PaintHistoryRibbonSection(HDC dc, RECT content, int& y, const EditorTheme& theme, const InspectorPanelState& inspector, const kb::scene::HistoryRibbonComponent& ribbon) {
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::HistoryRibbon, HeroIconKind::Cube, "Wst\xC4\x99" "ga historii", true);
    section.Field("Segment Mesh", std::to_string(ribbon.meshAssetId), InspectorPropertyId::HistoryRibbonMeshAssetId);
    section.Field("Material", std::to_string(ribbon.materialAssetId), InspectorPropertyId::HistoryRibbonMaterialAssetId);
    section.Field("Lifetime", FormatFloat(ribbon.lifetimeSeconds, 3), InspectorPropertyId::HistoryRibbonLifetimeSeconds);
    section.Field("Width", FormatFloat(ribbon.width, 3), InspectorPropertyId::HistoryRibbonWidth);
    section.Field("Sample Interval", FormatFloat(ribbon.sampleIntervalSeconds, 3), InspectorPropertyId::HistoryRibbonSampleIntervalSeconds);
    section.Field("Render Layer", std::to_string(ribbon.layer), InspectorPropertyId::HistoryRibbonLayer);
    section.Bool("Cast Shadows", ribbon.castsShadow, InspectorPropertyId::HistoryRibbonCastsShadow);
    section.Bool("Receive Shadows", ribbon.receivesShadow, InspectorPropertyId::HistoryRibbonReceivesShadow);
    section.Bool("Enabled", ribbon.enabled, InspectorPropertyId::HistoryRibbonEnabled);
    y = section.Bottom() + kSectionGap;
}

constexpr int kCameraSectionRows = 13;

void PaintCameraSection(
    HDC dc,
    RECT content,
    int& y,
    const EditorTheme& theme,
    const InspectorPanelState& inspector,
    const kb::scene::CameraComponent& camera) {
    SectionWriter section(
        dc,
        Rect(content.left, y, content.right, content.bottom),
        theme,
        inspector,
        InspectorSectionId::Camera,
        HeroIconKind::Eye,
        "Camera",
        true);
    section.Field(
        "Projection",
        InspectorComponentLabelFormatter::ProjectionName(camera.projection),
        InspectorPropertyId::CameraProjection);
    section.Float(
        "Vertical FOV",
        FormatFloat(camera.verticalFovDegrees, 2),
        InspectorPropertyId::CameraVerticalFov);
    section.Float(
        "Ortho Height",
        FormatFloat(camera.orthographicHeight, 2),
        InspectorPropertyId::CameraOrthographicHeight);
    section.Float(
        "Near Clip",
        FormatFloat(camera.nearClip, 3),
        InspectorPropertyId::CameraNearClip);
    section.Float(
        "Far Clip",
        FormatFloat(camera.farClip, 2),
        InspectorPropertyId::CameraFarClip);
    section.Bool("Primary", camera.primary, InspectorPropertyId::CameraPrimary);
    section.Field(
        "Viewport ID",
        std::to_string(camera.viewportId),
        InspectorPropertyId::CameraViewportId);
    section.Field(
        "Priority",
        std::to_string(camera.priority),
        InspectorPropertyId::CameraPriority);
    section.Field(
        "Culling Mask",
        std::to_string(camera.cullingMask),
        InspectorPropertyId::CameraCullingMask);
    section.Field(
        "Clear Mode",
        InspectorComponentLabelFormatter::CameraClearModeName(camera.clearMode),
        InspectorPropertyId::CameraClearMode);
    section.Float(
        "Clear Color R",
        FormatFloat(camera.clearColor.x, 3),
        InspectorPropertyId::CameraClearColorR);
    section.Float(
        "Clear Color G",
        FormatFloat(camera.clearColor.y, 3),
        InspectorPropertyId::CameraClearColorG);
    section.Float(
        "Clear Color B",
        FormatFloat(camera.clearColor.z, 3),
        InspectorPropertyId::CameraClearColorB);
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
    int rows = 11;
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
    SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::Light, HeroIconKind::Bolt, "3D Radiance Emitter");
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
    section.Bool("Use Color Temperature", light.useColorTemperature, InspectorPropertyId::LightUseColorTemperature);
    section.Float("Color Temperature (K)", FormatFloat(light.colorTemperatureKelvin, 0), InspectorPropertyId::LightColorTemperatureKelvin);
    section.Field("Layer Mask", std::to_string(light.layerMask), InspectorPropertyId::LightLayerMask);
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
    if (!slotRows.empty()) {
        section.Disclosure(
            "Advanced",
            InspectorPropertyId::MeshRendererAdvanced,
            inspector.IsDisclosureExpanded(InspectorDisclosureId::MeshRendererAdvanced));
    }
    for (const InspectorMeshRendererMaterialSlotRow& row : slotRows) {
        const std::string prefix = "Slot " + std::to_string(row.slotIndex + 1U);
        const std::string nameLabel = prefix + " Name";
        const std::string sourceLabel = prefix + " Source";
        const std::string defaultLabel = prefix + " Default";
        const std::array<DisplayField, 3> advancedFields{ {
            { nameLabel, row.slotName },
            { sourceLabel, row.importedSourceName },
            { defaultLabel, row.defaultMaterialName },
        } };
        section.AnimatedFields(
            advancedFields,
            inspector.DisclosureExpansion(InspectorDisclosureId::MeshRendererAdvanced),
            static_cast<int>(row.slotIndex * 3U));
        section.AssetField(row.label, row.overrideMaterialName, MeshRendererMaterialSlotProperty(row.slotIndex), MeshRendererMaterialSlotPickerProperty(row.slotIndex));
        section.Field(prefix + " Material Status", row.activeMaterialStatus);
        section.Field(prefix + " Sections", row.sectionsUsingSlot);
    }
    section.Bool("Casts Shadow", renderer.castsShadow, InspectorPropertyId::MeshRendererCastsShadow);
    section.Bool("Receives Shadow", renderer.receivesShadow, InspectorPropertyId::MeshRendererReceivesShadow);
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
[[nodiscard]] int MeshRendererSectionHeight(const EditorSceneContext& sceneContext, const kb::scene::MeshRendererComponent& renderer);

[[nodiscard]] RECT TagsDropdownAnchorRect(const RECT& content) noexcept {
    const int tagsRowTop = content.top + kHeaderHeight + kPanelPadTop + kSectionHeaderHeight + kDividerHeight
        + 2 * (kFieldRowHeight + kDividerHeight);
    const int labelRight = content.left + ((content.right - content.left) * 36 / 100);
    const int top = tagsRowTop + (kFieldRowHeight - kValueHeight) / 2;
    return Rect(labelRight, top, content.right - kRowPadX, top + kValueHeight);
}

constexpr int kTagsDropdownGap = 3;
constexpr int kTagsDropdownRowHeight = 28;
constexpr int kTagsDropdownPadding = 3;

[[nodiscard]] RECT TagsDropdownOptionRect(const RECT& anchor, int visualIndex) noexcept {
    const int top = anchor.bottom + kTagsDropdownGap + kTagsDropdownPadding + visualIndex * kTagsDropdownRowHeight;
    return Rect(anchor.left + kTagsDropdownPadding, top, anchor.right - kTagsDropdownPadding, top + kTagsDropdownRowHeight);
}

[[nodiscard]] RECT TagsDropdownRemoveRect(const RECT& option) noexcept {
    constexpr int kRemoveSize = 20;
    const int top = option.top + (option.bottom - option.top - kRemoveSize) / 2;
    return Rect(option.right - kRemoveSize - 4, top, option.right - 4, top + kRemoveSize);
}

void DrawRoundedFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    ScopedBrush brush(fill);
    ScopedPen pen(1, border);
    const ScopedGdiObject selectedBrush(dc, brush.handle);
    const ScopedGdiObject selectedPen(dc, pen.handle);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
}

void DrawTagsSelectionMark(HDC dc, const RECT& option, COLORREF color) {
    const int left = option.left + 9;
    const int middleY = option.top + (option.bottom - option.top) / 2;
    ScopedPen pen(2, color);
    const ScopedGdiObject selectedPen(dc, pen.handle);
    MoveToEx(dc, left, middleY, nullptr);
    LineTo(dc, left + 4, middleY + 4);
    LineTo(dc, left + 11, middleY - 4);
}

void DrawTagsRemoveIcon(HDC dc, const RECT& rect, COLORREF color) {
    const int centerX = rect.left + (rect.right - rect.left) / 2;
    const int centerY = rect.top + (rect.bottom - rect.top) / 2;
    constexpr int kRadius = 3;
    ScopedPen pen(1, color);
    const ScopedGdiObject selectedPen(dc, pen.handle);
    MoveToEx(dc, centerX - kRadius, centerY - kRadius, nullptr);
    LineTo(dc, centerX + kRadius + 1, centerY + kRadius + 1);
    MoveToEx(dc, centerX + kRadius, centerY - kRadius, nullptr);
    LineTo(dc, centerX - kRadius - 1, centerY + kRadius + 1);
}

void PaintTagsDropdown(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext, kb::scene::SceneEntity entity) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    if (!inspector.IsTagsDropdownOpen() || inspector.IsCollapsed(InspectorSectionId::General)) {
        return;
    }

    const std::vector<std::string> known = sceneContext.KnownSceneTags();
    const std::vector<std::string> selected = sceneContext.EntityTags(entity);
    const RECT anchor = TagsDropdownAnchorRect(content);
    const int noTagIndex = static_cast<int>(known.size());
    const int newTagIndex = noTagIndex + 1;
    const int optionCount = newTagIndex + 1;
    const int menuTop = anchor.bottom + kTagsDropdownGap;
    const RECT menu = Rect(anchor.left, menuTop, anchor.right,
        menuTop + optionCount * kTagsDropdownRowHeight + 2 * kTagsDropdownPadding);
    const COLORREF menuFill = BlendColor(Color(theme.strip), Color(theme.panel), 38);
    GdiDrawing::FillRectColor(dc, Rect(menu.left + 2, menu.top + 3, menu.right + 2, menu.bottom + 3), Rgb(12, 14, 17));
    DrawRoundedFrame(dc, menu, menuFill, BlendColor(Color(theme.borderPanel), Color(theme.chrome), 28), 5);

    for (int visualIndex = 0; visualIndex < optionCount; ++visualIndex) {
        const int actionIndex = visualIndex == 0
            ? noTagIndex
            : (visualIndex <= static_cast<int>(known.size()) ? visualIndex - 1 : newTagIndex);
        const RECT option = TagsDropdownOptionRect(anchor, visualIndex);
        const bool hovered = inspector.TagsDropdownHover() == actionIndex;
        const bool isTag = actionIndex < static_cast<int>(known.size());
        const bool isNoTag = actionIndex == noTagIndex;
        const bool isNewTag = actionIndex == newTagIndex;
        const bool isRemovableTag = isTag && !kb::scene::SceneTagCatalog::IsBuiltIn(known[static_cast<std::size_t>(actionIndex)]);

        std::string_view label;
        bool checked = false;
        if (isTag) {
            const std::string& tag = known[static_cast<std::size_t>(actionIndex)];
            label = tag;
            checked = std::find(selected.begin(), selected.end(), tag) != selected.end();
        } else if (isNoTag) {
            label = "None";
            checked = selected.empty();
        } else {
            label = "Add Tag";
        }

        if (hovered) {
            GdiDrawing::FillRectColor(dc, option, BlendColor(menuFill, Color(theme.textSecondary), 11));
        }

        ScopedFont optionFont(12, checked ? FW_SEMIBOLD : FW_NORMAL);
        const ScopedGdiObject selectedFont(dc, optionFont.handle);
        if (checked) {
            DrawTagsSelectionMark(dc, option, Color(theme.accent));
        }
        const RECT remove = TagsDropdownRemoveRect(option);
        const int textLeft = option.left + 28;
        const int textRight = isRemovableTag ? remove.left - 6 : option.right - 10;
        const COLORREF textColor = isNewTag
            ? (hovered ? Color(theme.accent) : Color(theme.textSecondary))
            : Color(theme.textPrimary);
        Text(dc, Rect(textLeft, option.top, textRight, option.bottom), label, textColor);
        if (isNewTag) {
            ScopedFont addFont(13, FW_NORMAL);
            const ScopedGdiObject selectedAddFont(dc, addFont.handle);
            Text(dc, Rect(option.left + 7, option.top, option.left + 27, option.bottom), "+", textColor,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        if (isRemovableTag) {
            const bool removeHovered = inspector.IsHovered(InspectorHitKind::TagOption, InspectorSectionId::General,
                InspectorPropertyId::TagsRemove, actionIndex);
            DrawTagsRemoveIcon(dc, remove, removeHovered ? RGB(224, 104, 104) : Color(theme.textDisabled));
        }
        if (visualIndex == 0 || visualIndex == optionCount - 2) {
            GdiDrawing::FillRectColor(dc, Rect(option.left + 7, option.bottom - 1, option.right - 7, option.bottom),
                BlendColor(Color(theme.borderPanel), menuFill, 52));
        }
    }
}

void PaintEntity(HDC dc, RECT content, const RECT& viewport, const EditorTheme& theme, const EditorSceneContext& sceneContext, kb::scene::SceneEntity selected) {
    const kb::scene::Scene& scene = sceneContext.Scene();
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const std::string title = scene.Entities().Name(selected);
    const std::string subtitle = "Entity " + FormatUInt64(selected.Id());

    DrawHeader(
        dc,
        content,
        theme,
        scene.Components().Cameras().Has(selected)
            ? HeroIconKind::Camera
            : HeroIconKind::Cube,
        title,
        subtitle);
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
        const int h = SectionHeight(inspector, InspectorSectionId::General, 3);
        if (sectionVisible(y, h)) {
            SectionWriter section(dc, Rect(content.left, y, content.right, content.bottom), theme, inspector, InspectorSectionId::General, HeroIconKind::AdjustmentsHorizontal, "General");
            section.Field("Name", scene.Entities().Name(selected), InspectorPropertyId::EntityName);
            section.Bool("Visible", visibility.mode != kb::scene::VisibilityMode::Hidden, InspectorPropertyId::EntityVisible);
            const std::vector<std::string> assignedTags = sceneContext.EntityTags(selected);
            section.Tag("Tag", assignedTags.empty() ? std::string_view{} : std::string_view{ assignedTags.front() }, InspectorPropertyId::TagsText);
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

    if (const kb::scene::RegionShapeComponent* regionShape = scene.Components().RegionShapes().TryGet(selected); regionShape != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::RegionShape, 6);
        if (sectionVisible(y, h)) {
            PaintRegionShapeSection(dc, content, y, theme, inspector, *regionShape);
        } else {
            y += h + kSectionGap;
        }
    }
    if (const kb::scene::GuideCurveComponent* guideCurve = scene.Components().GuideCurves().TryGet(selected); guideCurve != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::GuideCurve, 4);
        if (y < content.bottom && y + h > content.top) PaintGuideCurveSection(dc, content, y, theme, inspector, *guideCurve); else y += h + kSectionGap;
    }
    if (const kb::scene::ContentInstanceComponent* contentInstance = scene.Components().ContentInstances().TryGet(selected); contentInstance != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::ContentInstance, 4);
        if (y < content.bottom && y + h > content.top) PaintContentInstanceSection(dc, content, y, theme, inspector, *contentInstance); else y += h + kSectionGap;
    }
    if (const kb::scene::StreamFocusComponent* streamFocus = scene.Components().StreamFocuses().TryGet(selected); streamFocus != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::StreamFocus, 5);
        if (y < content.bottom && y + h > content.top) PaintStreamFocusSection(dc, content, y, theme, inspector, *streamFocus); else y += h + kSectionGap;
    }
    if (const kb::scene::WorldBackdropComponent* backdrop = scene.Components().WorldBackdrops().TryGet(selected); backdrop != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::WorldBackdrop, 15);
        if (y < content.bottom && y + h > content.top) PaintWorldBackdropSection(dc, content, y, theme, inspector, *backdrop); else y += h + kSectionGap;
    }
    if (const kb::scene::AmbientRadianceComponent* ambient = scene.Components().AmbientRadiances().TryGet(selected); ambient != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::AmbientRadiance, 16);
        if (y < content.bottom && y + h > content.top) PaintAmbientRadianceSection(dc, content, y, theme, inspector, *ambient); else y += h + kSectionGap;
    }
    if (const kb::scene::SceneDetailSwitchComponent* detailSwitch = scene.Components().DetailSwitches().TryGet(selected); detailSwitch != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::DetailSwitch, 6);
        if (y < content.bottom && y + h > content.top) PaintDetailSwitchSection(dc, content, y, theme, inspector, *detailSwitch); else y += h + kSectionGap;
    }
    if (const kb::scene::SceneVisibilityBlockerComponent* blocker = scene.Components().VisibilityBlockers().TryGet(selected); blocker != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::VisibilityBlocker, 7);
        if (y < content.bottom && y + h > content.top) PaintVisibilityBlockerSection(dc, content, y, theme, inspector, *blocker); else y += h + kSectionGap;
    }
    if (const kb::scene::VisibilityCellComponent* cell = scene.Components().VisibilityCells().TryGet(selected); cell != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::VisibilityCell, 4);
        if (y < content.bottom && y + h > content.top) PaintVisibilityCellSection(dc, content, y, theme, inspector, *cell); else y += h + kSectionGap;
    }
    if (const kb::scene::SceneRegionPortalComponent* portal = scene.Components().RegionPortals().TryGet(selected); portal != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::RegionPortal, 4);
        if (y < content.bottom && y + h > content.top) PaintRegionPortalSection(dc, content, y, theme, inspector, *portal); else y += h + kSectionGap;
    }
    if (const kb::scene::AuxFrameComponent* frame = scene.Components().AuxFrames().TryGet(selected); frame != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::SecondaryFrame, 9);
        if (y < content.bottom && y + h > content.top) PaintSecondaryFrameSection(dc, content, y, theme, inspector, *frame); else y += h + kSectionGap;
    }
    if (const kb::scene::GeometrySwarmComponent* swarm = scene.Components().GeometrySwarms().TryGet(selected); swarm != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::GeometrySwarm, 14);
        if (y < content.bottom && y + h > content.top) PaintGeometrySwarmSection(dc, content, y, theme, inspector, *swarm); else y += h + kSectionGap;
    }
    if (const kb::scene::SurfaceCastComponent* surfaceCast = scene.Components().SurfaceCasts().TryGet(selected); surfaceCast != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::SurfaceCast, 5);
        if (y < content.bottom && y + h > content.top) PaintSurfaceCastSection(dc, content, y, theme, inspector, *surfaceCast); else y += h + kSectionGap;
    }
    if (const kb::scene::FacingPanelComponent* panel = scene.Components().FacingPanels().TryGet(selected); panel != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::FacingPanel, 11);
        if (y < content.bottom && y + h > content.top) PaintFacingPanelSection(dc, content, y, theme, inspector, *panel); else y += h + kSectionGap;
    }
    if (const kb::scene::SpaceStrokeComponent* stroke = scene.Components().SpaceStrokes().TryGet(selected); stroke != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::SpaceStroke, 10);
        if (y < content.bottom && y + h > content.top) PaintSpaceStrokeSection(dc, content, y, theme, inspector, *stroke); else y += h + kSectionGap;
    }
    if (const kb::scene::HistoryRibbonComponent* ribbon = scene.Components().HistoryRibbons().TryGet(selected); ribbon != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::HistoryRibbon, 9);
        if (y < content.bottom && y + h > content.top) PaintHistoryRibbonSection(dc, content, y, theme, inspector, *ribbon); else y += h + kSectionGap;
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
    if (const kb::scene::CameraComponent* camera =
            scene.Components().Cameras().TryGet(selected);
        camera != nullptr) {
        const int h =
            SectionHeight(inspector, InspectorSectionId::Camera, kCameraSectionRows);
        if (sectionVisible(y, h)) {
            PaintCameraSection(dc, content, y, theme, inspector, *camera);
        } else {
            y += h + kSectionGap;
        }
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
        const int h = MeshRendererSectionHeight(sceneContext, *meshRenderer);
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
    if (const kb::scene::Animator* animator = scene.Components().Animators().TryGet(selected); animator != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::Animator, 4);
        if (sectionVisible(y, h)) {
            PaintAnimatorSection(dc, content, y, theme, inspector, sceneContext, *animator);
        } else {
            y += h + kSectionGap;
        }
    }
    if (const kb::scene::UIDocumentComponent* document = scene.Components().UIDocuments().TryGet(selected); document != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::UIDocument, 2);
        if (sectionVisible(y, h)) {
            PaintUIDocumentSection(dc, content, y, theme, inspector, sceneContext, *document);
        } else {
            y += h + kSectionGap;
        }
    }
    if (const kb::scene::NavAgent* agent = scene.Components().NavAgents().TryGet(selected); agent != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::NavAgent, 9);
        if (sectionVisible(y, h)) {
            PaintNavAgentSection(dc, content, y, theme, inspector, *agent);
        } else {
            y += h + kSectionGap;
        }
    }
    if (const kb::scene::NavObstacle* obstacle = scene.Components().NavObstacles().TryGet(selected); obstacle != nullptr) {
        const int h = SectionHeight(inspector, InspectorSectionId::NavObstacle, 8);
        if (sectionVisible(y, h)) {
            PaintNavObstacleSection(dc, content, y, theme, inspector, *obstacle);
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
    PaintTagsDropdown(dc, content, theme, sceneContext, selected);
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
        // Preview only: no Material or Asset sections to reserve room for, so the panel stops pretending
        // it has content to scroll to.
        height += MaterialPreviewSectionHeight(inspector, MaterialPreviewTelemetryFor(sceneContext, metadata), MaterialDebugChannelRowsFor(sceneContext, metadata).size());
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

[[nodiscard]] int MeshRendererAdvancedMetadataHeight(const InspectorPanelState& inspector) noexcept {
    return static_cast<int>(std::lround(
        3.0F * static_cast<float>(kFieldRowHeight + kDividerHeight)
        * inspector.DisclosureExpansion(InspectorDisclosureId::MeshRendererAdvanced)));
}

// Must match PaintMeshRendererSection exactly. Slot metadata occupies an
// animated clipped band while the override/status/section rows stay available.
[[nodiscard]] int MeshRendererSectionHeight(const EditorSceneContext& sceneContext, const kb::scene::MeshRendererComponent& renderer) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    if (inspector.IsCollapsed(InspectorSectionId::MeshRenderer)) {
        return kSectionHeaderHeight;
    }
    const std::optional<kb::render::RenderMeshAssetData> mesh = LoadMeshAssetData(sceneContext, renderer.meshAssetId);
    const std::uint32_t slotCount = InspectorMeshRendererMaterialSlotModel::SlotRowCount(renderer, mesh);
    const int fixedRows = 4 + 3 * static_cast<int>(slotCount);
    const int disclosureHeight = slotCount == 0U ? 0 : kDisclosureRowHeight + kDividerHeight;
    return kSectionHeaderHeight + kDividerHeight
        + fixedRows * (kFieldRowHeight + kDividerHeight)
        + disclosureHeight
        + static_cast<int>(slotCount) * MeshRendererAdvancedMetadataHeight(inspector);
}

[[nodiscard]] int EntityContentHeight(const EditorSceneContext& sceneContext, kb::scene::SceneEntity selected) {
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const kb::scene::Scene& scene = sceneContext.Scene();
    int height = kHeaderHeight + kPanelPadTop;
    height += SectionHeight(inspector, InspectorSectionId::General, 3) + kSectionGap;
    height += SectionHeight(inspector, InspectorSectionId::Transform, 3) + kSectionGap;
    if (scene.Components().RegionShapes().Has(selected)) {
        height += SectionHeight(inspector, InspectorSectionId::RegionShape, 6) + kSectionGap;
    }
    if (scene.Components().GuideCurves().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::GuideCurve, 4) + kSectionGap;
    if (scene.Components().ContentInstances().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::ContentInstance, 4) + kSectionGap;
    if (scene.Components().StreamFocuses().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::StreamFocus, 5) + kSectionGap;
    if (scene.Components().WorldBackdrops().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::WorldBackdrop, 15) + kSectionGap;
    if (scene.Components().AmbientRadiances().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::AmbientRadiance, 16) + kSectionGap;
    if (scene.Components().DetailSwitches().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::DetailSwitch, 6) + kSectionGap;
    if (scene.Components().VisibilityBlockers().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::VisibilityBlocker, 7) + kSectionGap;
    if (scene.Components().VisibilityCells().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::VisibilityCell, 4) + kSectionGap;
    if (scene.Components().RegionPortals().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::RegionPortal, 4) + kSectionGap;
    if (scene.Components().AuxFrames().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::SecondaryFrame, 9) + kSectionGap;
    if (scene.Components().GeometrySwarms().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::GeometrySwarm, 14) + kSectionGap;
    if (scene.Components().SurfaceCasts().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::SurfaceCast, 5) + kSectionGap;
    if (scene.Components().FacingPanels().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::FacingPanel, 11) + kSectionGap;
    if (scene.Components().SpaceStrokes().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::SpaceStroke, 10) + kSectionGap;
    if (scene.Components().HistoryRibbons().Has(selected)) height += SectionHeight(inspector, InspectorSectionId::HistoryRibbon, 9) + kSectionGap;
    if (sceneContext.HasEntityScript(selected)) {
        const int scriptRows = 2 + static_cast<int>(sceneContext.EntityScriptExposedVariables(selected).size());
        height += SectionHeight(inspector, InspectorSectionId::Script, scriptRows) + kSectionGap;
    }
    if (scene.Components().Cameras().Has(selected)) {
        height +=
            SectionHeight(inspector, InspectorSectionId::Camera, kCameraSectionRows) +
            kSectionGap;
    }
    if (const kb::scene::LightComponent* light = scene.Components().Lights().TryGet(selected); light != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::Light, LightSectionRows(*light)) + kSectionGap;
    }
    if (const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(selected); renderer != nullptr) {
        height += MeshRendererSectionHeight(sceneContext, *renderer) + kSectionGap;
    }
    if (scene.Components().AudioSources().TryGet(selected) != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::AudioSource, 10) + kSectionGap;
    }
    if (scene.Components().AudioListeners().TryGet(selected) != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::AudioListener, 2) + kSectionGap;
    }
    if (scene.Components().Animators().TryGet(selected) != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::Animator, 4) + kSectionGap;
    }
    if (scene.Components().UIDocuments().TryGet(selected) != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::UIDocument, 2) + kSectionGap;
    }
    if (scene.Components().NavAgents().TryGet(selected) != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::NavAgent, 9) + kSectionGap;
    }
    if (scene.Components().NavObstacles().TryGet(selected) != nullptr) {
        height += SectionHeight(inspector, InspectorSectionId::NavObstacle, 8) + kSectionGap;
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

[[nodiscard]] InspectorPanelRenderer::Hit HitTestCameraSection(
    const RECT& content,
    const InspectorPanelState& state,
    int x,
    int yPoint,
    int& y) noexcept {
    if (InspectorPanelRenderer::Hit hit =
            HitSectionHeader(
                content,
                y,
                state,
                InspectorSectionId::Camera,
                x,
                yPoint,
                true);
        hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (state.IsCollapsed(InspectorSectionId::Camera)) {
        return {};
    }

    if (InspectorPanelRenderer::Hit hit =
            HitTextRow(
                RowRect(content, y),
                InspectorSectionId::Camera,
                InspectorPropertyId::CameraProjection,
                x,
                yPoint);
        hit.kind != InspectorHitKind::None) {
        return hit;
    }
    AdvanceRow(y);
    for (const InspectorPropertyId property : {
             InspectorPropertyId::CameraVerticalFov,
             InspectorPropertyId::CameraOrthographicHeight,
             InspectorPropertyId::CameraNearClip,
             InspectorPropertyId::CameraFarClip,
         }) {
        if (InspectorPanelRenderer::Hit hit =
                HitFloatRow(
                    RowRect(content, y),
                    InspectorSectionId::Camera,
                    property,
                    x,
                    yPoint);
            hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
    }
    if (InspectorPanelRenderer::Hit hit =
            HitBool(
                RowRect(content, y),
                InspectorSectionId::Camera,
                InspectorPropertyId::CameraPrimary,
                x,
                yPoint);
        hit.kind != InspectorHitKind::None) {
        return hit;
    }
    AdvanceRow(y);
    for (const InspectorPropertyId property : {
             InspectorPropertyId::CameraViewportId,
             InspectorPropertyId::CameraPriority,
             InspectorPropertyId::CameraCullingMask,
             InspectorPropertyId::CameraClearMode,
         }) {
        if (InspectorPanelRenderer::Hit hit =
                HitTextRow(
                    RowRect(content, y),
                    InspectorSectionId::Camera,
                    property,
                    x,
                    yPoint);
            hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
    }
    for (const InspectorPropertyId property : {
             InspectorPropertyId::CameraClearColorR,
             InspectorPropertyId::CameraClearColorG,
             InspectorPropertyId::CameraClearColorB,
         }) {
        if (InspectorPanelRenderer::Hit hit =
                HitFloatRow(
                    RowRect(content, y),
                    InspectorSectionId::Camera,
                    property,
                    x,
                    yPoint);
            hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
    }
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
    if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(content, y), InspectorSectionId::Light, InspectorPropertyId::LightUseColorTemperature, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    AdvanceRow(y);
    if (InspectorPanelRenderer::Hit hit = HitLightFloatRow(content, y, InspectorPropertyId::LightColorTemperatureKelvin, x, yPoint); hit.kind != InspectorHitKind::None) {
        return hit;
    }
    if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(content, y), InspectorSectionId::Light, InspectorPropertyId::LightLayerMask, x, yPoint); hit.kind != InspectorHitKind::None) {
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

std::optional<RECT> InspectorPanelRenderer::AddComponentOverlayRect(const RECT& content, const EditorSceneContext& sceneContext) {
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
    const int buttonY = content.top - inspectorScroll + total - kAddComponentButtonHeight;
    return AddComponentBrowserRect(viewport, buttonY);
}

void InspectorPanelRenderer::PaintAddComponentOverlay(HDC dc, const RECT& bounds, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    if (!sceneContext.Inspector().IsAddComponentBrowserOpen()) {
        return;
    }
    DrawAddComponentBrowser(dc, bounds, theme, sceneContext.Inspector());
}

InspectorPanelRenderer::AddComponentScrollInfo InspectorPanelRenderer::AddComponentOverlayScrollGeometry(const RECT& bounds, const EditorSceneContext& sceneContext) {
    if (!sceneContext.Inspector().IsAddComponentBrowserOpen()) {
        return {};
    }
    const InspectorPanelState& state = sceneContext.Inspector();
    const std::string_view query = AddComponentQuery(state);
    const std::string& category = state.AddComponentBrowserCategory();
    const bool showBack = query.empty() && !category.empty();
    const std::vector<AddComponentRow> rows = InspectorAddComponentBrowserModel::Rows(query.empty() ? std::string_view{ category } : std::string_view{}, query);
    const RECT list = AddComponentListRect(bounds, showBack);
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

bool InspectorPanelRenderer::AddComponentOverlayListContains(const RECT& bounds, const EditorSceneContext& sceneContext, int x, int y) {
    if (!sceneContext.Inspector().IsAddComponentBrowserOpen()) {
        return false;
    }
    const InspectorPanelState& state = sceneContext.Inspector();
    const std::string_view query = AddComponentQuery(state);
    const std::string& category = state.AddComponentBrowserCategory();
    const bool showBack = query.empty() && !category.empty();
    const RECT list = AddComponentListRect(bounds, showBack);
    return x >= list.left && x < list.right && y >= list.top && y < list.bottom;
}

InspectorPanelRenderer::Hit InspectorPanelRenderer::HitTestAddComponentOverlay(
    const RECT& bounds,
    const EditorSceneContext& sceneContext,
    int x,
    int y) noexcept {
    const InspectorPanelState& state = sceneContext.Inspector();
    if (!state.IsAddComponentBrowserOpen() || !Contains(bounds, x, y)) {
        return {};
    }
    const RECT search = AddComponentSearchRect(bounds);
    if (Contains(search, x, y)) {
        return MakeHit(InspectorHitKind::TextField, InspectorSectionId::AddComponent, InspectorPropertyId::AddComponentSearch, search);
    }
    const std::string_view query = AddComponentQuery(state);
    const std::string& category = state.AddComponentBrowserCategory();
    const bool showBack = query.empty() && !category.empty();
    if (showBack) {
        const RECT back = AddComponentBackHeaderRect(bounds);
        if (Contains(back, x, y)) {
            return MakeHit(InspectorHitKind::TextField, InspectorSectionId::AddComponent, InspectorPropertyId::AddComponentBack, back);
        }
    }
    const std::vector<AddComponentRow> rows =
        InspectorAddComponentBrowserModel::Rows(query.empty() ? std::string_view{ category } : std::string_view{}, query);
    const RECT list = AddComponentListRect(bounds, showBack);
    const int listHeight = static_cast<int>(list.bottom - list.top);
    const int rowCount = static_cast<int>(rows.size());
    const bool scrollable = InspectorAddComponentBrowserModel::TotalHeight(rowCount, kAddComponentRowHeight) > listHeight;
    const int scroll = std::clamp(
        state.AddComponentScroll(),
        0,
        InspectorAddComponentBrowserModel::MaxScroll(rowCount, kAddComponentRowHeight, listHeight));
    if (scrollable) {
        const RECT thumb = AddComponentScrollbarThumbRect(list, rowCount, scroll);
        if (thumb.bottom > thumb.top && Contains(thumb, x, y)) {
            return MakeHit(InspectorHitKind::ScrollbarThumb, InspectorSectionId::AddComponent, InspectorPropertyId::None, thumb);
        }
        const RECT track = AddComponentScrollbarTrackRect(list);
        if (Contains(track, x, y)) {
            return MakeHit(InspectorHitKind::ScrollbarTrack, InspectorSectionId::AddComponent, InspectorPropertyId::None, track);
        }
    }
    const RECT inner = AddComponentListInnerRect(list, scrollable);
    if (y < list.top || y >= list.bottom || x < inner.left || x >= inner.right) {
        return {};
    }
    const InspectorAddComponentBrowserModel::VisibleWindow window =
        InspectorAddComponentBrowserModel::Visible(rowCount, scroll, kAddComponentRowHeight, listHeight);
    for (int index = window.first; index < window.first + window.count; ++index) {
        const int rowTop = list.top - scroll + index * kAddComponentRowHeight;
        const RECT rowRect = Rect(inner.left, rowTop, inner.right, rowTop + kAddComponentRowHeight);
        if (Contains(rowRect, x, y)) {
            Hit hit = MakeHit(InspectorHitKind::TextField, InspectorSectionId::AddComponent, InspectorPropertyId::AddComponentOption, rowRect);
            hit.index = index;
            return hit;
        }
    }
    return {};
}

std::optional<RECT> InspectorPanelRenderer::MaterialPreviewRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    // Returning nullopt here used to disable the Inspector's 3D preview surface entirely, which is why the
    // panel could only ever show the flat software ball.
    const kb::assets::AssetId assetId = sceneContext.AssetBrowser().InspectorAsset();
    if (!assetId.IsValid()) {
        return std::nullopt;
    }
    const kb::assets::AssetMetadata* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance")) {
        return std::nullopt;
    }
    if (sceneContext.Inspector().IsCollapsed(InspectorSectionId::MaterialPreview)) {
        return std::nullopt;
    }
    // The surface has to travel with the panel exactly like the painted rows do: same scrolled content
    // rect, same viewport clip. Reading the unscrolled panel rect left it pinned in place while the rest
    // of the Inspector scrolled underneath it.
    const int maxScroll = MaxScrollOffset(content, sceneContext);
    const bool scrollable = maxScroll > 0;
    const int scroll = std::clamp(sceneContext.Inspector().ScrollOffset(), 0, maxScroll);
    RECT scrolled = ContentViewportRect(content, scrollable);
    OffsetRect(&scrolled, 0, -scroll);
    const RECT viewport = ContentViewportRect(content, scrollable);

    const RECT frame = MaterialPreviewFrameRect(scrolled);
    RECT visible{};
    if (IntersectRect(&visible, &frame, &viewport) == 0) {
        // Scrolled out of sight: no surface at all, rather than one hanging over the rows below.
        return std::nullopt;
    }
    const RECT inset{ visible.left + 1, visible.top + 1, visible.right - 1, visible.bottom - 1 };
    if (inset.right - inset.left <= 8 || inset.bottom - inset.top <= 8) {
        return std::nullopt;
    }
    return inset;
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
        if (state.IsTagsDropdownOpen()) {
            const RECT anchor = TagsDropdownAnchorRect(viewport);
            const std::vector<std::string> knownTags = sceneContext.KnownSceneTags();
            const int optionCount = static_cast<int>(knownTags.size()) + 2;
            const int noTagIndex = static_cast<int>(knownTags.size());
            const int newTagIndex = noTagIndex + 1;
            for (int visualIndex = 0; visualIndex < optionCount; ++visualIndex) {
                const int actionIndex = visualIndex == 0
                    ? noTagIndex
                    : (visualIndex <= static_cast<int>(knownTags.size()) ? visualIndex - 1 : newTagIndex);
                const RECT rect = TagsDropdownOptionRect(anchor, visualIndex);
                if (Contains(rect, x, scrolledY)) {
                    InspectorPropertyId property = InspectorPropertyId::TagsText;
                    if (actionIndex >= 0 && actionIndex < static_cast<int>(knownTags.size())
                        && !kb::scene::SceneTagCatalog::IsBuiltIn(knownTags[static_cast<std::size_t>(actionIndex)])) {
                        if (Contains(TagsDropdownRemoveRect(rect), x, scrolledY)) {
                            property = InspectorPropertyId::TagsRemove;
                        }
                    }
                    return InspectorPanelRenderer::Hit{
                        .kind = InspectorHitKind::TagOption,
                        .section = InspectorSectionId::General,
                        .property = property,
                        .index = actionIndex,
                        .rect = rect,
                    };
                }
            }
        }
        if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::General, InspectorPropertyId::EntityName, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
        if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::General, InspectorPropertyId::EntityVisible, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        AdvanceRow(y);
        if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::General, InspectorPropertyId::TagsText, x, scrolledY); hit.kind != InspectorHitKind::None) {
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

    if (sceneContext.Scene().Components().RegionShapes().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::RegionShape, x, scrolledY, true); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (!state.IsCollapsed(InspectorSectionId::RegionShape)) {
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::RegionShape, InspectorPropertyId::RegionShapeKind, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitVec3(RowRect(viewport, y), InspectorSectionId::RegionShape, InspectorPropertyId::RegionShapeCenterX, InspectorPropertyId::RegionShapeCenterY, InspectorPropertyId::RegionShapeCenterZ, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitVec3(RowRect(viewport, y), InspectorSectionId::RegionShape, InspectorPropertyId::RegionShapeSizeX, InspectorPropertyId::RegionShapeSizeY, InspectorPropertyId::RegionShapeSizeZ, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::RegionShape, InspectorPropertyId::RegionShapeRadius, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::RegionShape, InspectorPropertyId::RegionShapeHeight, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::RegionShape, InspectorPropertyId::RegionShapeEnabled, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
        }
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().GuideCurves().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::GuideCurve, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::GuideCurve)) {
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::GuideCurve, InspectorPropertyId::GuideCurveControlPointCount, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::GuideCurve, InspectorPropertyId::GuideCurveInterpolation, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::GuideCurve, InspectorPropertyId::GuideCurveClosed, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::GuideCurve, InspectorPropertyId::GuideCurveEnabled, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
        }
    }
    if (sceneContext.Scene().Components().ContentInstances().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::ContentInstance, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::ContentInstance)) {
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::ContentInstance, InspectorPropertyId::ContentInstanceAssetId, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::ContentInstance, InspectorPropertyId::ContentInstanceKind, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::ContentInstance, InspectorPropertyId::ContentInstanceLifetime, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::ContentInstance, InspectorPropertyId::ContentInstanceActive, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
        }
    }
    if (sceneContext.Scene().Components().StreamFocuses().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::StreamFocus, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::StreamFocus)) {
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::StreamFocus, InspectorPropertyId::StreamFocusInnerRadius, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::StreamFocus, InspectorPropertyId::StreamFocusOuterRadius, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::StreamFocus, InspectorPropertyId::StreamFocusPriority, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::StreamFocus, InspectorPropertyId::StreamFocusLoadMask, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::StreamFocus, InspectorPropertyId::StreamFocusEnabled, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
        }
    }
    if (sceneContext.Scene().Components().WorldBackdrops().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::WorldBackdrop, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::WorldBackdrop)) {
            constexpr std::array<InspectorPropertyId, 15> properties{ InspectorPropertyId::WorldBackdropMode, InspectorPropertyId::WorldBackdropColorR, InspectorPropertyId::WorldBackdropColorG, InspectorPropertyId::WorldBackdropColorB, InspectorPropertyId::WorldBackdropHorizonColorR, InspectorPropertyId::WorldBackdropHorizonColorG, InspectorPropertyId::WorldBackdropHorizonColorB, InspectorPropertyId::WorldBackdropZenithColorR, InspectorPropertyId::WorldBackdropZenithColorG, InspectorPropertyId::WorldBackdropZenithColorB, InspectorPropertyId::WorldBackdropEnvironmentAssetId, InspectorPropertyId::WorldBackdropHorizonHeight, InspectorPropertyId::WorldBackdropGradientExponent, InspectorPropertyId::WorldBackdropPriority, InspectorPropertyId::WorldBackdropEnabled };
            for (const InspectorPropertyId property : properties) {
                const InspectorPanelRenderer::Hit hit = property == InspectorPropertyId::WorldBackdropEnabled
                    ? HitBool(RowRect(viewport, y), InspectorSectionId::WorldBackdrop, property, x, scrolledY)
                    : HitTextRow(RowRect(viewport, y), InspectorSectionId::WorldBackdrop, property, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
        }
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().AmbientRadiances().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::AmbientRadiance, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::AmbientRadiance)) {
            constexpr std::array<InspectorPropertyId, 16> properties{ InspectorPropertyId::AmbientRadianceMode, InspectorPropertyId::AmbientRadianceColorR, InspectorPropertyId::AmbientRadianceColorG, InspectorPropertyId::AmbientRadianceColorB, InspectorPropertyId::AmbientRadianceHorizonColorR, InspectorPropertyId::AmbientRadianceHorizonColorG, InspectorPropertyId::AmbientRadianceHorizonColorB, InspectorPropertyId::AmbientRadianceZenithColorR, InspectorPropertyId::AmbientRadianceZenithColorG, InspectorPropertyId::AmbientRadianceZenithColorB, InspectorPropertyId::AmbientRadianceEnvironmentAssetId, InspectorPropertyId::AmbientRadianceIntensity, InspectorPropertyId::AmbientRadianceDiffuseIntensity, InspectorPropertyId::AmbientRadianceSpecularIntensity, InspectorPropertyId::AmbientRadiancePriority, InspectorPropertyId::AmbientRadianceEnabled };
            for (const InspectorPropertyId property : properties) {
                const InspectorPanelRenderer::Hit hit = property == InspectorPropertyId::AmbientRadianceEnabled
                    ? HitBool(RowRect(viewport, y), InspectorSectionId::AmbientRadiance, property, x, scrolledY)
                    : HitTextRow(RowRect(viewport, y), InspectorSectionId::AmbientRadiance, property, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
        }
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().DetailSwitches().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::DetailSwitch, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::DetailSwitch)) {
            constexpr std::array<InspectorPropertyId, 6> properties{ InspectorPropertyId::DetailSwitchGroupId, InspectorPropertyId::DetailSwitchMinimumLod, InspectorPropertyId::DetailSwitchMaximumLod, InspectorPropertyId::DetailSwitchPromoteCoverage, InspectorPropertyId::DetailSwitchDemoteCoverage, InspectorPropertyId::DetailSwitchEnabled };
            for (const InspectorPropertyId property : properties) {
                const InspectorPanelRenderer::Hit hit = property == InspectorPropertyId::DetailSwitchEnabled
                    ? HitBool(RowRect(viewport, y), InspectorSectionId::DetailSwitch, property, x, scrolledY)
                    : HitTextRow(RowRect(viewport, y), InspectorSectionId::DetailSwitch, property, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
        }
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().VisibilityBlockers().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::VisibilityBlocker, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::VisibilityBlocker)) {
            constexpr std::array<InspectorPropertyId, 7> properties{ InspectorPropertyId::VisibilityBlockerCenterX, InspectorPropertyId::VisibilityBlockerCenterY, InspectorPropertyId::VisibilityBlockerCenterZ, InspectorPropertyId::VisibilityBlockerSizeX, InspectorPropertyId::VisibilityBlockerSizeY, InspectorPropertyId::VisibilityBlockerSizeZ, InspectorPropertyId::VisibilityBlockerEnabled };
            for (InspectorPropertyId property : properties) {
                const InspectorPanelRenderer::Hit hit = property == InspectorPropertyId::VisibilityBlockerEnabled ? HitBool(RowRect(viewport, y), InspectorSectionId::VisibilityBlocker, property, x, scrolledY) : HitTextRow(RowRect(viewport, y), InspectorSectionId::VisibilityBlocker, property, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
        }
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().VisibilityCells().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::VisibilityCell, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::VisibilityCell)) {
            constexpr std::array<InspectorPropertyId, 4> properties{ InspectorPropertyId::VisibilityCellMembershipMask, InspectorPropertyId::VisibilityCellMembership, InspectorPropertyId::VisibilityCellOverride, InspectorPropertyId::VisibilityCellEnabled };
            for (InspectorPropertyId property : properties) {
                const InspectorPanelRenderer::Hit hit = property == InspectorPropertyId::VisibilityCellEnabled ? HitBool(RowRect(viewport, y), InspectorSectionId::VisibilityCell, property, x, scrolledY) : HitTextRow(RowRect(viewport, y), InspectorSectionId::VisibilityCell, property, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
        }
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().RegionPortals().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::RegionPortal, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::RegionPortal)) {
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::RegionPortal, InspectorPropertyId::RegionPortalSourceCell, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::RegionPortal, InspectorPropertyId::RegionPortalTargetCell, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::RegionPortal, InspectorPropertyId::RegionPortalPurposes, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::RegionPortal, InspectorPropertyId::RegionPortalEnabled, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
        }
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().AuxFrames().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::SecondaryFrame, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::SecondaryFrame)) {
            constexpr std::array<InspectorPropertyId, 9> properties{ InspectorPropertyId::SecondaryFrameMode, InspectorPropertyId::SecondaryFrameImageTargetId, InspectorPropertyId::SecondaryFrameWidth, InspectorPropertyId::SecondaryFrameHeight, InspectorPropertyId::SecondaryFramePlaneNormalX, InspectorPropertyId::SecondaryFramePlaneNormalY, InspectorPropertyId::SecondaryFramePlaneNormalZ, InspectorPropertyId::SecondaryFramePlaneOffset, InspectorPropertyId::SecondaryFrameEnabled };
            for (InspectorPropertyId property : properties) {
                const InspectorPanelRenderer::Hit hit = property == InspectorPropertyId::SecondaryFrameEnabled ? HitBool(RowRect(viewport, y), InspectorSectionId::SecondaryFrame, property, x, scrolledY) : HitTextRow(RowRect(viewport, y), InspectorSectionId::SecondaryFrame, property, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
        }
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().GeometrySwarms().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::GeometrySwarm, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::GeometrySwarm)) {
            constexpr std::array<InspectorPropertyId, 14> properties{ InspectorPropertyId::GeometrySwarmMeshAssetId, InspectorPropertyId::GeometrySwarmMaterialAssetId, InspectorPropertyId::GeometrySwarmInstanceCount, InspectorPropertyId::GeometrySwarmColumns, InspectorPropertyId::GeometrySwarmRows, InspectorPropertyId::GeometrySwarmLayers, InspectorPropertyId::GeometrySwarmSpacingX, InspectorPropertyId::GeometrySwarmSpacingY, InspectorPropertyId::GeometrySwarmSpacingZ, InspectorPropertyId::GeometrySwarmInstanceScale, InspectorPropertyId::GeometrySwarmLayer, InspectorPropertyId::GeometrySwarmCastsShadow, InspectorPropertyId::GeometrySwarmReceivesShadow, InspectorPropertyId::GeometrySwarmEnabled };
            for (InspectorPropertyId property : properties) {
                const bool isBool = property == InspectorPropertyId::GeometrySwarmCastsShadow || property == InspectorPropertyId::GeometrySwarmReceivesShadow || property == InspectorPropertyId::GeometrySwarmEnabled;
                const InspectorPanelRenderer::Hit hit = isBool ? HitBool(RowRect(viewport, y), InspectorSectionId::GeometrySwarm, property, x, scrolledY) : HitTextRow(RowRect(viewport, y), InspectorSectionId::GeometrySwarm, property, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
        }
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().SurfaceCasts().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::SurfaceCast, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::SurfaceCast)) {
            constexpr std::array<InspectorPropertyId, 5> properties{ InspectorPropertyId::SurfaceCastMaterialAssetId, InspectorPropertyId::SurfaceCastReceiverLayerMask, InspectorPropertyId::SurfaceCastOrder, InspectorPropertyId::SurfaceCastContent, InspectorPropertyId::SurfaceCastEnabled };
            for (InspectorPropertyId property : properties) {
                const bool isBool = property == InspectorPropertyId::SurfaceCastEnabled;
                const InspectorPanelRenderer::Hit hit = isBool ? HitBool(RowRect(viewport, y), InspectorSectionId::SurfaceCast, property, x, scrolledY) : HitTextRow(RowRect(viewport, y), InspectorSectionId::SurfaceCast, property, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
        } else y += kHeaderHeight;
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().FacingPanels().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::FacingPanel, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::FacingPanel)) {
            constexpr std::array<InspectorPropertyId, 11> properties{ InspectorPropertyId::FacingPanelMode, InspectorPropertyId::FacingPanelTargetX, InspectorPropertyId::FacingPanelTargetY, InspectorPropertyId::FacingPanelTargetZ, InspectorPropertyId::FacingPanelAxisX, InspectorPropertyId::FacingPanelAxisY, InspectorPropertyId::FacingPanelAxisZ, InspectorPropertyId::FacingPanelUpX, InspectorPropertyId::FacingPanelUpY, InspectorPropertyId::FacingPanelUpZ, InspectorPropertyId::FacingPanelEnabled };
            for (InspectorPropertyId property : properties) {
                const InspectorPanelRenderer::Hit hit = property == InspectorPropertyId::FacingPanelEnabled ? HitBool(RowRect(viewport, y), InspectorSectionId::FacingPanel, property, x, scrolledY) : HitTextRow(RowRect(viewport, y), InspectorSectionId::FacingPanel, property, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
        } else y += kHeaderHeight;
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().SpaceStrokes().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::SpaceStroke, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::SpaceStroke)) {
            constexpr std::array<InspectorPropertyId, 10> properties{ InspectorPropertyId::SpaceStrokeMeshAssetId, InspectorPropertyId::SpaceStrokeMaterialAssetId, InspectorPropertyId::SpaceStrokeMode, InspectorPropertyId::SpaceStrokeWidth, InspectorPropertyId::SpaceStrokeCableSag, InspectorPropertyId::SpaceStrokeSplineSegments, InspectorPropertyId::SpaceStrokeLayer, InspectorPropertyId::SpaceStrokeCastsShadow, InspectorPropertyId::SpaceStrokeReceivesShadow, InspectorPropertyId::SpaceStrokeEnabled };
            for (InspectorPropertyId property : properties) {
                const bool isBool = property == InspectorPropertyId::SpaceStrokeCastsShadow || property == InspectorPropertyId::SpaceStrokeReceivesShadow || property == InspectorPropertyId::SpaceStrokeEnabled;
                const InspectorPanelRenderer::Hit hit = isBool ? HitBool(RowRect(viewport, y), InspectorSectionId::SpaceStroke, property, x, scrolledY) : HitTextRow(RowRect(viewport, y), InspectorSectionId::SpaceStroke, property, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
        } else y += kHeaderHeight;
        y += kSectionGap;
    }
    if (sceneContext.Scene().Components().HistoryRibbons().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::HistoryRibbon, x, scrolledY, true); hit.kind != InspectorHitKind::None) return hit;
        if (!state.IsCollapsed(InspectorSectionId::HistoryRibbon)) {
            constexpr std::array<InspectorPropertyId, 9> properties{ InspectorPropertyId::HistoryRibbonMeshAssetId, InspectorPropertyId::HistoryRibbonMaterialAssetId, InspectorPropertyId::HistoryRibbonLifetimeSeconds, InspectorPropertyId::HistoryRibbonWidth, InspectorPropertyId::HistoryRibbonSampleIntervalSeconds, InspectorPropertyId::HistoryRibbonLayer, InspectorPropertyId::HistoryRibbonCastsShadow, InspectorPropertyId::HistoryRibbonReceivesShadow, InspectorPropertyId::HistoryRibbonEnabled };
            for (InspectorPropertyId property : properties) {
                const bool isBool = property == InspectorPropertyId::HistoryRibbonCastsShadow || property == InspectorPropertyId::HistoryRibbonReceivesShadow || property == InspectorPropertyId::HistoryRibbonEnabled;
                const InspectorPanelRenderer::Hit hit = isBool ? HitBool(RowRect(viewport, y), InspectorSectionId::HistoryRibbon, property, x, scrolledY) : HitTextRow(RowRect(viewport, y), InspectorSectionId::HistoryRibbon, property, x, scrolledY);
                if (hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
        } else y += kHeaderHeight;
        y += kSectionGap;
    }

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

    if (sceneContext.Scene().Components().Cameras().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit =
                HitTestCameraSection(viewport, state, x, scrolledY, y);
            hit.kind != InspectorHitKind::None) {
            return hit;
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
            if (!slotRows.empty()) {
                const RECT advanced = Rect(content.left, y, content.right, y + kDisclosureRowHeight);
                if (Contains(advanced, x, scrolledY)) {
                    return MakeHit(InspectorHitKind::Row, InspectorSectionId::MeshRenderer, InspectorPropertyId::MeshRendererAdvanced, advanced);
                }
                y += kDisclosureRowHeight + kDividerHeight;
            }
            for (const InspectorMeshRendererMaterialSlotRow& row : slotRows) {
                const int metadataHeight = MeshRendererAdvancedMetadataHeight(state);
                const int metadataRight = content.right;
                const int metadataLeft = content.left + kDisclosureTextOffset - kRowPadX;
                const int metadataBottom = y + metadataHeight;
                for (int fieldIndex = 0; fieldIndex < 3; ++fieldIndex) {
                    const RECT fieldRow = RowRect(
                        Rect(metadataLeft, viewport.top, metadataRight, viewport.bottom),
                        y + fieldIndex * (kFieldRowHeight + kDividerHeight));
                    RECT visibleRow = fieldRow;
                    visibleRow.bottom = std::min<LONG>(visibleRow.bottom, metadataBottom);
                    if (visibleRow.bottom > visibleRow.top && Contains(visibleRow, x, scrolledY)) {
                        InspectorPanelRenderer::Hit hit = HitTextRow(
                            fieldRow,
                            InspectorSectionId::MeshRenderer,
                            InspectorPropertyId::None,
                            x,
                            scrolledY);
                        hit.index = static_cast<int>(row.slotIndex * 3U) + fieldIndex;
                        return hit;
                    }
                }
                y += metadataHeight;
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
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::MeshRenderer, InspectorPropertyId::MeshRendererCastsShadow, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::MeshRenderer, InspectorPropertyId::MeshRendererReceivesShadow, x, scrolledY); hit.kind != InspectorHitKind::None) {
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

    if (sceneContext.Scene().Components().Animators().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::Animator, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (!state.IsCollapsed(InspectorSectionId::Animator)) {
            if (InspectorPanelRenderer::Hit hit = HitRows(viewport, y, InspectorSectionId::Animator, kAnimatorRows, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
        }
        y += kSectionGap;
    }

    if (sceneContext.Scene().Components().UIDocuments().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::UIDocument, x, scrolledY); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (!state.IsCollapsed(InspectorSectionId::UIDocument)) {
            if (InspectorPanelRenderer::Hit hit = HitRows(viewport, y, InspectorSectionId::UIDocument, kUIDocumentRows, x, scrolledY); hit.kind != InspectorHitKind::None) {
                return hit;
            }
        }
        y += kSectionGap;
    }

    if (sceneContext.Scene().Components().NavAgents().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::NavAgent, x, scrolledY, true); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (!state.IsCollapsed(InspectorSectionId::NavAgent)) {
            const std::array<InspectorPropertyId, 7> scalarRows{ {
                InspectorPropertyId::NavAgentRadius, InspectorPropertyId::NavAgentHeight, InspectorPropertyId::NavAgentMaxSpeed,
                InspectorPropertyId::NavAgentAcceleration, InspectorPropertyId::NavAgentAngularSpeed, InspectorPropertyId::NavAgentStoppingDistance,
                InspectorPropertyId::NavAgentAreaMask,
            } };
            for (const InspectorPropertyId property : scalarRows) {
                if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::NavAgent, property, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
            if (InspectorPanelRenderer::Hit hit = HitVec3(RowRect(viewport, y), InspectorSectionId::NavAgent, InspectorPropertyId::NavAgentDestinationX, InspectorPropertyId::NavAgentDestinationY, InspectorPropertyId::NavAgentDestinationZ, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::NavAgent, InspectorPropertyId::NavAgentEnabled, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
        }
        y += kSectionGap;
    }

    if (sceneContext.Scene().Components().NavObstacles().Has(selected)) {
        if (InspectorPanelRenderer::Hit hit = HitSectionHeader(viewport, y, state, InspectorSectionId::NavObstacle, x, scrolledY, true); hit.kind != InspectorHitKind::None) {
            return hit;
        }
        if (!state.IsCollapsed(InspectorSectionId::NavObstacle)) {
            if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::NavObstacle, InspectorPropertyId::NavObstacleShape, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitVec3(RowRect(viewport, y), InspectorSectionId::NavObstacle, InspectorPropertyId::NavObstacleCenterX, InspectorPropertyId::NavObstacleCenterY, InspectorPropertyId::NavObstacleCenterZ, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitVec3(RowRect(viewport, y), InspectorSectionId::NavObstacle, InspectorPropertyId::NavObstacleSizeX, InspectorPropertyId::NavObstacleSizeY, InspectorPropertyId::NavObstacleSizeZ, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            const std::array<InspectorPropertyId, 3> scalarRows{ { InspectorPropertyId::NavObstacleRadius, InspectorPropertyId::NavObstacleHeight, InspectorPropertyId::NavObstacleArea } };
            for (const InspectorPropertyId property : scalarRows) {
                if (InspectorPanelRenderer::Hit hit = HitTextRow(RowRect(viewport, y), InspectorSectionId::NavObstacle, property, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
                AdvanceRow(y);
            }
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::NavObstacle, InspectorPropertyId::NavObstacleCarve, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
            if (InspectorPanelRenderer::Hit hit = HitBool(RowRect(viewport, y), InspectorSectionId::NavObstacle, InspectorPropertyId::NavObstacleEnabled, x, scrolledY); hit.kind != InspectorHitKind::None) return hit;
            AdvanceRow(y);
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
    return {};
}

} // namespace kb::editor

#endif
