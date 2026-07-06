#include "rendering/MaterialEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "rendering/EditorTexturePreviewService.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/material_preview/EditorMaterialGraphCookService.hpp"
#include "scene/material_preview/EditorMaterialPreviewTelemetry.hpp"

#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

constexpr int kHeaderHeight = MaterialEditorPanelMetrics::HeaderHeight;
constexpr int kPadding = MaterialEditorPanelMetrics::Padding;
constexpr int kGraphNodeHeaderHeight = MaterialEditorPanelMetrics::GraphNodeHeaderHeight;
constexpr int kGraphNodeBodyTopPadding = MaterialEditorPanelMetrics::GraphNodeBodyTopPadding;
constexpr int kGraphNodePinRowHeight = MaterialEditorPanelMetrics::GraphNodePinRowHeight;
constexpr int kGraphNodePinRadius = 6;
// Cosmic redesign: sharp, angular "sci-fi terminal" panels instead of rounded Blender-style nodes --
// a small bevel (not a literal 0) keeps FillRoundedRect/StrokeRoundedRect's corner math well-defined.
constexpr int kGraphNodeCornerDiameter = 3;
constexpr int kGraphTitleFontSize = 11;
constexpr int kGraphPinFontSize = 10;
constexpr int kGraphMinTextPointSize = 9;

// Cosmic redesign: one unified dark-blue/near-black "space station terminal" look for every node,
// replacing Blender's per-category header tinting -- the user asked for every node to look the same.
namespace BlenderGraphTheme {
constexpr COLORREF NodeBody = RGB(11, 13, 20);
constexpr COLORREF NodeBodyBottom = RGB(7, 8, 13);
constexpr COLORREF NodeOutline = RGB(42, 58, 92);
constexpr COLORREF NodeOutlineSelected = RGB(96, 210, 255);
constexpr COLORREF NodeShadow = RGB(0, 0, 0);
constexpr COLORREF NodeHeader = RGB(26, 42, 78);
constexpr COLORREF Text = RGB(226, 232, 245);
constexpr COLORREF TextMuted = RGB(158, 172, 200);
constexpr COLORREF Field = RGB(15, 18, 27);
constexpr COLORREF FieldBorder = RGB(30, 42, 66);
constexpr COLORREF FieldFocus = RGB(69, 140, 210);
constexpr COLORREF SliderFill = RGB(46, 84, 128);
constexpr COLORREF SliderFillFocus = RGB(59, 128, 190);
constexpr COLORREF LinkShadow = RGB(0, 0, 0);
constexpr COLORREF LinkFallback = RGB(150, 168, 200);
constexpr COLORREF Canvas = RGB(6, 7, 13);
constexpr COLORREF GridDot = RGB(40, 54, 82);
constexpr COLORREF GridDotMajor = RGB(52, 70, 104);
// GridDot alpha-blended over Canvas at 60/255, precomputed: SetPixelV is a single cheap GDI call,
// while GdiDrawing::FillRectAlpha allocates a whole compatible DC + bitmap per call -- ruinous for
// the thousands of minor-grid dots drawn per repaint (measured ~35ms/frame from this alone).
constexpr COLORREF GridDotBlended = RGB(14, 18, 29);
} // namespace BlenderGraphTheme

void DrawText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize = 12, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, static_cast<int>(flags | DT_NOPREFIX));
}

void EnsureMaterialGraphFontRegistered() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    std::array<wchar_t, 32768> modulePath{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) {
        return;
    }
    const std::filesystem::path fontPath = std::filesystem::path{ modulePath.data() }.parent_path() / L"Content" / L"EditorShell" / L"Fonts" / L"DejaVuSans.ttf";
    static_cast<void>(AddFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr));
}

[[nodiscard]] int GraphTextLogicalHeight(HDC dc, int pointSize) noexcept {
    const int clampedPointSize = std::clamp(pointSize, kGraphMinTextPointSize, 18);
    return -MulDiv(clampedPointSize, GetDeviceCaps(dc, LOGPIXELSY), 72);
}

// The node graph can draw dozens of text labels per repaint (a node's title plus one label per
// pin -- a Material Output node alone has 14). CreateFontW/DeleteObject per call used to dominate
// node-drawing time; logical height only depends on point size and the DC's DPI (constant for the
// window's lifetime absent a DPI change), so cache fonts by (logical height, weight) instead.
// Session-static and intentionally never deleted, same rationale as other long-lived GDI resources
// in this file: font handles are cheap to keep and there are only a handful of distinct sizes/weights.
struct GraphTextFontStats {
    std::uint64_t hitCount = 0U;
    std::uint64_t missCount = 0U;
    std::size_t distinctKeys = 0U;
};

[[nodiscard]] GraphTextFontStats& GraphTextFontCacheStats() {
    static GraphTextFontStats stats;
    return stats;
}

[[nodiscard]] HFONT GraphTextFont(HDC dc, int pointSize, int weight) {
    static std::unordered_map<std::int64_t, HFONT> fonts;
    const int logicalHeight = GraphTextLogicalHeight(dc, pointSize);
    const std::int64_t key = (static_cast<std::int64_t>(logicalHeight) << 32) | static_cast<std::int64_t>(weight);
    if (const auto found = fonts.find(key); found != fonts.end()) {
        ++GraphTextFontCacheStats().hitCount;
        return found->second;
    }
    ++GraphTextFontCacheStats().missCount;
    HFONT font = CreateFontW(
        logicalHeight,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_NATURAL_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    fonts.emplace(key, font);
    GraphTextFontCacheStats().distinctKeys = fonts.size();
    return font;
}

void DrawGraphText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    EnsureMaterialGraphFontRegistered();
    HFONT font = GraphTextFont(dc, pointSize, weight);
    const ScopedGdiObject selectedFont(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, static_cast<int>(flags | DT_NOPREFIX));
}

void DrawCommandButton(HDC dc, const RECT& rect, const char* label, bool emphasized) {
    const COLORREF fill = emphasized ? RGB(42, 58, 47) : RGB(38, 41, 46);
    const COLORREF border = emphasized ? RGB(83, 122, 91) : RGB(58, 63, 70);
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    DrawText(dc, RECT{ rect.left + 8, rect.top, rect.right - 8, rect.bottom }, label, RGB(221, 226, 232), 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

[[nodiscard]] const char* PreviewPrimitiveButtonLabel(EditorMaterialPreviewPrimitiveKind kind) noexcept {
    switch (kind) {
    case EditorMaterialPreviewPrimitiveKind::Sphere: return "Sphere";
    case EditorMaterialPreviewPrimitiveKind::Cylinder: return "Cylinder";
    case EditorMaterialPreviewPrimitiveKind::Cube: return "Cube";
    case EditorMaterialPreviewPrimitiveKind::Plane: return "Plane";
    case EditorMaterialPreviewPrimitiveKind::CustomMesh: return "Custom";
    case EditorMaterialPreviewPrimitiveKind::Fallback: return "Fallback";
    }
    return "Preview";
}

[[nodiscard]] const char* PreviewSceneButtonLabel(EditorMaterialPreviewLightingPreset preset) noexcept {
    switch (preset) {
    case EditorMaterialPreviewLightingPreset::Studio: return "Studio";
    case EditorMaterialPreviewLightingPreset::Neutral: return "Neutral";
    case EditorMaterialPreviewLightingPreset::HighContrast: return "High";
    }
    return "Scene";
}

[[nodiscard]] const char* PreviewQualityButtonLabel(kb::render::RenderMaterialGraphQualityLevel qualityLevel) noexcept {
    switch (qualityLevel) {
    case kb::render::RenderMaterialGraphQualityLevel::Low: return "Low";
    case kb::render::RenderMaterialGraphQualityLevel::Medium: return "Med";
    case kb::render::RenderMaterialGraphQualityLevel::High: return "High";
    case kb::render::RenderMaterialGraphQualityLevel::Epic: return "Epic";
    }
    return "Quality";
}

void DrawHeader(HDC dc, const RECT& content, const EditorSceneContext& sceneContext, bool dirty, bool infoVisible) {
    const RECT header{ content.left, content.top, content.right, content.top + kHeaderHeight };
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    GdiDrawing::FillRectColor(dc, header, RGB(32, 35, 39));
    GdiDrawing::FillRectColor(dc, RECT{ header.left, header.bottom - 1, header.right, header.bottom }, RGB(13, 14, 16));
    DrawText(dc, RECT{ header.left + kPadding, header.top, layout.infoButton.left - 10, header.bottom }, "Material Editor", RGB(226, 230, 235), 14, FW_SEMIBOLD);
    DrawCommandButton(dc, layout.infoButton, "Info", infoVisible);
    DrawCommandButton(dc, layout.previewPrimitiveButton, PreviewPrimitiveButtonLabel(sceneContext.MaterialPreviewPrimitivePolicy().kind), false);
    DrawCommandButton(dc, layout.previewSceneButton, PreviewSceneButtonLabel(sceneContext.MaterialPreviewSceneSettings().lightingPreset), false);
    DrawCommandButton(dc, layout.previewQualityButton, PreviewQualityButtonLabel(sceneContext.MaterialPreviewSceneSettings().qualityLevel), false);
    DrawCommandButton(dc, layout.previewNodeButton, "Node", sceneContext.MaterialPreviewNodePreviewEnabled());
    DrawCommandButton(dc, layout.applyButton, "Apply To Selection", false);
    DrawCommandButton(dc, layout.saveButton, "Save", dirty);
    DrawCommandButton(dc, layout.revertButton, "Revert", false);
    DrawCommandButton(dc, layout.validateButton, "Validate", false);
    if (dirty) {
        DrawText(dc, RECT{ header.left + kPadding, header.top, layout.infoButton.left - 10, header.bottom }, "Unsaved changes", RGB(223, 178, 91), 11, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

[[nodiscard]] RECT PreviewFrameRect(const RECT& content) noexcept {
    return MaterialEditorPanelRenderer::ResolveLayout(content).previewFrame;
}

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0, static_cast<int>(rect.bottom - rect.top));
}

[[nodiscard]] int RectWidth(const RECT& rect) noexcept {
    return std::max(0, static_cast<int>(rect.right - rect.left));
}

[[nodiscard]] int ScaleMetric(int value, float scale) noexcept {
    return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * scale)));
}

[[nodiscard]] float NodeScale(const RECT& rect) noexcept {
    return static_cast<float>(RectWidth(rect)) / static_cast<float>(std::max(1, MaterialEditorPanelMetrics::GraphNodeWidth));
}

[[nodiscard]] float NodeUiScale(const RECT& rect, kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    const SIZE graphNodeSize = MaterialEditorPanelGraphNodeSize(kind);
    return static_cast<float>(RectWidth(rect)) / static_cast<float>(std::max<LONG>(1, graphNodeSize.cx));
}

[[nodiscard]] BYTE ColorChannel(COLORREF color, int shift) noexcept {
    return static_cast<BYTE>((color >> shift) & 0xFF);
}

[[nodiscard]] COLORREF LerpColor(COLORREF from, COLORREF to, int index, int count) noexcept {
    const int denom = std::max(1, count - 1);
    const int r = ColorChannel(from, 0) + (((ColorChannel(to, 0) - ColorChannel(from, 0)) * index) / denom);
    const int g = ColorChannel(from, 8) + (((ColorChannel(to, 8) - ColorChannel(from, 8)) * index) / denom);
    const int b = ColorChannel(from, 16) + (((ColorChannel(to, 16) - ColorChannel(from, 16)) * index) / denom);
    return RGB(r, g, b);
}

[[nodiscard]] COLORREF ScaleColor(COLORREF color, float scale) noexcept {
    const int r = std::clamp(static_cast<int>(static_cast<float>(ColorChannel(color, 0)) * scale), 0, 255);
    const int g = std::clamp(static_cast<int>(static_cast<float>(ColorChannel(color, 8)) * scale), 0, 255);
    const int b = std::clamp(static_cast<int>(static_cast<float>(ColorChannel(color, 16)) * scale), 0, 255);
    return RGB(r, g, b);
}

[[nodiscard]] Gdiplus::Color ToGdiplusColor(COLORREF color, BYTE alpha = 255U) noexcept {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void DrawVerticalGradient(HDC dc, const RECT& rect, COLORREF top, COLORREF bottom) {
    const int height = RectHeight(rect);
    const int width = RectWidth(rect);
    if (height <= 0 || width <= 0) {
        return;
    }
    // The gradient used to be painted one CreateSolidBrush+FillRect per scanline (100+ GDI object
    // allocations per node) and, after that, via GradientFill -- which turned out to not be
    // hardware-accelerated on this machine's driver and cost about as much. Computing the lerped
    // column in plain memory and stretching it onto the target in a single StretchDIBits call
    // avoids GDI object churn and any driver-dependent gradient path entirely.
    std::vector<std::uint32_t> column(static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const COLORREF blended = LerpColor(top, bottom, y, height);
        column[static_cast<std::size_t>(y)] = (static_cast<std::uint32_t>(GetRValue(blended)) << 16U) |
            (static_cast<std::uint32_t>(GetGValue(blended)) << 8U) | static_cast<std::uint32_t>(GetBValue(blended));
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 1;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const int oldMode = SetStretchBltMode(dc, COLORONCOLOR);
    static_cast<void>(StretchDIBits(dc, rect.left, rect.top, width, height, 0, 0, 1, height, column.data(), &info, DIB_RGB_COLORS, SRCCOPY));
    SetStretchBltMode(dc, oldMode);
}

void FillRoundedRect(HDC dc, const RECT& rect, COLORREF fill, int cornerDiameter) {
    HPEN pen = static_cast<HPEN>(GetStockObject(NULL_PEN));
    HBRUSH brush = CreateSolidBrush(fill);
    {
        const ScopedGdiObject selectedPen(dc, pen);
        const ScopedGdiObject selectedBrush(dc, brush);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, cornerDiameter, cornerDiameter);
    }
    DeleteObject(brush);
}

void StrokeRoundedRect(HDC dc, const RECT& rect, COLORREF color, int cornerDiameter, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HBRUSH brush = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    {
        const ScopedGdiObject selectedPen(dc, pen);
        const ScopedGdiObject selectedBrush(dc, brush);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, cornerDiameter, cornerDiameter);
    }
    DeleteObject(pen);
}

void AddRoundedRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    const float diameter = std::min(std::min(rect.Width, rect.Height), radius * 2.0F);
    if (diameter <= 1.0F) {
        path.AddRectangle(rect);
        return;
    }
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270.0F, 90.0F);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0.0F, 90.0F);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90.0F, 90.0F);
    path.CloseFigure();
}

// Takes an already-constructed Gdiplus::Graphics: DrawGraphNodeFrame calls this three times per
// node (a layered drop shadow) and Gdiplus::Graphics construction is expensive enough that doing
// it per-call, per-shadow-layer, per-node dominated node-drawing time -- the same issue as the
// per-pin Graphics construction fixed earlier.
void FillRoundedRectAlpha(Gdiplus::Graphics& graphics, const RECT& rect, COLORREF color, BYTE alpha, int cornerDiameter) {
    Gdiplus::GraphicsPath path;
    AddRoundedRectPath(
        path,
        Gdiplus::RectF{
            static_cast<Gdiplus::REAL>(rect.left),
            static_cast<Gdiplus::REAL>(rect.top),
            static_cast<Gdiplus::REAL>(std::max(0, static_cast<int>(rect.right - rect.left))),
            static_cast<Gdiplus::REAL>(std::max(0, static_cast<int>(rect.bottom - rect.top))),
        },
        static_cast<float>(cornerDiameter) * 0.5F);
    Gdiplus::SolidBrush brush(ToGdiplusColor(color, alpha));
    graphics.FillPath(&brush, &path);
}

// Convenience overload for the low-frequency call sites (comment/composite boxes, drawn once each
// per repaint, not per-node) that don't already have a Graphics handy.
void FillRoundedRectAlpha(HDC dc, const RECT& rect, COLORREF color, BYTE alpha, int cornerDiameter) {
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    FillRoundedRectAlpha(graphics, rect, color, alpha, cornerDiameter);
}

void DrawVerticalGradientClippedToRound(HDC dc, const RECT& rect, const RECT& clip, COLORREF top, COLORREF bottom, int cornerDiameter) {
    // The node corner radius is 1-2px (kGraphNodeCornerDiameter=3, minus inset) -- visually a hard
    // corner. CreateRoundRectRgn+ExtSelectClipRgn(RGN_AND) combines a fresh region with whatever
    // complex clip region is already active on the DC (the graph canvas clip, and above that the
    // back buffer's dirty-rect clip), which is a real region-boolean cost, not a cheap rect compare
    // -- called twice per node, it dominated node-drawing time during a full-window repaint (the
    // common case while dragging, since the drag invalidates the whole window). A plain rect clip
    // is visually indistinguishable at this radius and is a trivial rect-intersect, no region object.
    static_cast<void>(cornerDiameter);
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    DrawVerticalGradient(dc, rect, top, bottom);
    RestoreDC(dc, savedDc);
}

[[nodiscard]] int GraphInputPinIndex(std::string_view pin) noexcept {
    if (pin == "baseColor") return 0;
    if (pin == "normal") return 1;
    if (pin == "roughness") return 2;
    if (pin == "metallic") return 3;
    if (pin == "emissive") return 4;
    if (pin == "occlusion") return 5;
    if (pin == "alpha") return 6;
    if (pin == "texture") return 0;
    if (pin == "uv") return 1;
    if (pin == "value") return 0;
    if (pin == "a") return 0;
    if (pin == "b") return 1;
    if (pin == "x") return 0;
    if (pin == "y") return 1;
    if (pin == "z") return 2;
    if (pin == "w") return 3;
    if (pin == "edge") return 0;
    if (pin == "base") return 0;
    if (pin == "exponent") return 1;
    if (pin == "min") return 1;
    if (pin == "max") return 2;
    if (pin == "t") return 2;
    if (pin == "color") return 0;
    if (pin == "fraction") return 1;
    if (pin == "view") return 1;
    if (pin == "less") return 2;
    if (pin == "equal") return 3;
    if (pin == "greater") return 4;
    return 0;
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>> GraphInputPins(kb::render::RenderMaterialGraphNodeKind kind);
[[nodiscard]] std::vector<std::pair<std::string, std::string>> GraphInputPins(const kb::render::RenderMaterialGraphNode& node);
[[nodiscard]] std::vector<std::pair<std::string, std::string>> GraphOutputPins(const kb::render::RenderMaterialGraphNode& node);

// Turn a camelCase pin name (e.g. "baseColor") into a readable label ("Base Color"). Used as the fallback
// label for nodes that are not in the editor's explicit pin tables, so every node renders sensible pins.
[[nodiscard]] std::string HumanizePinName(std::string_view pin) {
    std::string label;
    label.reserve(pin.size() + 4U);
    for (std::size_t i = 0U; i < pin.size(); ++i) {
        const char c = pin[i];
        if (i == 0U) {
            label.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        } else if (c >= 'A' && c <= 'Z') {
            label.push_back(' ');
            label.push_back(c);
        } else {
            label.push_back(c);
        }
    }
    return label.empty() ? std::string{ pin } : label;
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>> GraphPinFallback(std::vector<std::string> names) {
    std::vector<std::pair<std::string, std::string>> pins;
    pins.reserve(names.size());
    for (std::string& name : names) {
        std::string label = HumanizePinName(name);
        pins.emplace_back(std::move(name), std::move(label));
    }
    return pins;
}

[[nodiscard]] POINT InputPinPoint(const RECT& node, std::string_view pin) noexcept {
    const int index = GraphInputPinIndex(pin);
    const float scale = NodeScale(node);
    const int pinInset = ScaleMetric(6, scale);
    return POINT{
        node.left + pinInset,
        node.top + ScaleMetric(kGraphNodeHeaderHeight, scale) + ScaleMetric(kGraphNodeBodyTopPadding, scale) + (index * ScaleMetric(kGraphNodePinRowHeight, scale)) + (ScaleMetric(kGraphNodePinRowHeight, scale) / 2),
    };
}

[[nodiscard]] POINT OutputPinPoint(const RECT& node) noexcept {
    const float scale = NodeScale(node);
    const int pinInset = ScaleMetric(6, scale);
    const int headerHeight = ScaleMetric(kGraphNodeHeaderHeight, scale);
    return POINT{ node.right - pinInset, node.top + headerHeight + ((RectHeight(node) - headerHeight) / 2) };
}

[[nodiscard]] POINT InputPinPoint(const RECT& node, kb::render::RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    // The pin's row is its position within this node's own input list, so every node (including those
    // resolved via the renderer fallback) lays its pins out correctly without a global name->row map.
    const std::vector<std::pair<std::string, std::string>> pins = GraphInputPins(kind);
    int index = 0;
    for (std::size_t i = 0U; i < pins.size(); ++i) {
        if (pins[i].first == pin) {
            index = static_cast<int>(i);
            break;
        }
    }
    const float scale = NodeUiScale(node, kind);
    const int pinInset = ScaleMetric(6, scale);
    const int headerHeight = ScaleMetric(kGraphNodeHeaderHeight, scale);
    const int count = static_cast<int>(std::max<std::size_t>(1U, pins.size()));
    const int bodyTop = node.top + headerHeight;
    const int bodyHeight = std::max(1, RectHeight(node) - headerHeight);
    const int rowHeight = std::min(ScaleMetric(kGraphNodePinRowHeight, scale), std::max(ScaleMetric(16, scale), bodyHeight / count));
    const int total = count * rowHeight;
    const int bodyCenter = bodyTop + (bodyHeight / 2);
    const int y = bodyCenter - (total / 2) + (index * rowHeight) + (rowHeight / 2);
    const int bottom = static_cast<int>(node.bottom);
    return POINT{
        node.left + pinInset,
        std::clamp(y, bodyTop + pinInset, bottom - pinInset),
    };
}

[[nodiscard]] POINT InputPinPoint(const RECT& nodeRect, const kb::render::RenderMaterialGraphNode& node, std::string_view pin) noexcept {
    const std::vector<std::pair<std::string, std::string>> pins = GraphInputPins(node);
    int index = 0;
    for (std::size_t i = 0U; i < pins.size(); ++i) {
        if (pins[i].first == pin) {
            index = static_cast<int>(i);
            break;
        }
    }
    const float scale = NodeUiScale(nodeRect, node.kind);
    const int pinInset = ScaleMetric(6, scale);
    const int headerHeight = ScaleMetric(kGraphNodeHeaderHeight, scale);
    const int count = static_cast<int>(std::max<std::size_t>(1U, pins.size()));
    const int bodyTop = nodeRect.top + headerHeight;
    const int bodyHeight = std::max(1, RectHeight(nodeRect) - headerHeight);
    const int rowHeight = std::min(ScaleMetric(kGraphNodePinRowHeight, scale), std::max(ScaleMetric(16, scale), bodyHeight / count));
    const int total = count * rowHeight;
    const int bodyCenter = bodyTop + (bodyHeight / 2);
    const int y = bodyCenter - (total / 2) + (index * rowHeight) + (rowHeight / 2);
    const int bottom = static_cast<int>(nodeRect.bottom);
    return POINT{
        nodeRect.left + pinInset,
        std::clamp(y, bodyTop + pinInset, bottom - pinInset),
    };
}

[[nodiscard]] POINT OutputPinPoint(const RECT& node, std::size_t index, std::size_t count) noexcept {
    if (count <= 1U) {
        return OutputPinPoint(node);
    }
    const float scale = NodeScale(node);
    const int pinInset = ScaleMetric(6, scale);
    const int headerHeight = ScaleMetric(kGraphNodeHeaderHeight, scale);
    const int rowHeight = ScaleMetric(kGraphNodePinRowHeight, scale);
    const int total = static_cast<int>(count) * rowHeight;
    const int top = static_cast<int>(node.top);
    const int bottom = static_cast<int>(node.bottom);
    return POINT{
        node.right - pinInset,
        std::clamp(
            top + headerHeight + ((RectHeight(node) - headerHeight) / 2) - (total / 2) + (static_cast<int>(index) * rowHeight) + (rowHeight / 2),
            top + headerHeight + pinInset,
            bottom - pinInset),
    };
}

[[nodiscard]] POINT OutputPinPoint(const RECT& node, kb::render::RenderMaterialGraphNodeKind kind, std::size_t index, std::size_t count) noexcept {
    const float scale = NodeUiScale(node, kind);
    const int pinInset = ScaleMetric(6, scale);
    if (count <= 1U) {
        const int headerHeight = ScaleMetric(kGraphNodeHeaderHeight, scale);
        return POINT{ node.right - pinInset, node.top + headerHeight + ((RectHeight(node) - headerHeight) / 2) };
    }
    if (!MaterialEditorPanelIsTextureSamplePreviewNode(kind)) {
        const int headerHeight = ScaleMetric(kGraphNodeHeaderHeight, scale);
        const int bodyTop = node.top + headerHeight;
        const int bodyHeight = std::max(1, RectHeight(node) - headerHeight);
        const int rowHeight = std::min(ScaleMetric(kGraphNodePinRowHeight, scale), std::max(ScaleMetric(16, scale), bodyHeight / static_cast<int>(count)));
        const int total = static_cast<int>(count) * rowHeight;
        const int bodyCenter = bodyTop + (bodyHeight / 2);
        const int y = bodyCenter - (total / 2) + (static_cast<int>(index) * rowHeight) + (rowHeight / 2);
        const int bottom = static_cast<int>(node.bottom);
        return POINT{
            node.right - pinInset,
            std::clamp(y, bodyTop + pinInset, bottom - pinInset),
        };
    }
    const int headerHeight = ScaleMetric(kGraphNodeHeaderHeight, scale);
    const int previewTop = node.top + headerHeight + ScaleMetric(8, scale);
    const int previewBottom = node.bottom - ScaleMetric(10, scale);
    const int previewHeight = std::max(1, previewBottom - previewTop);
    const int rowHeight = std::min(
        ScaleMetric(kGraphNodePinRowHeight, scale),
        std::max(ScaleMetric(16, scale), previewHeight / static_cast<int>(count)));
    const int total = static_cast<int>(count) * rowHeight;
    const int bodyCenter = previewTop + (previewHeight / 2);
    const int y = bodyCenter - (total / 2) + (static_cast<int>(index) * rowHeight) + (rowHeight / 2);
    return POINT{
        node.right - pinInset,
        std::clamp(y, previewTop + pinInset, previewBottom - pinInset),
    };
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>> GraphInputPins(kb::render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return {
            { "baseColor", "Base Color" },
            { "normal", "Normal" },
            { "roughness", "Roughness" },
            { "metallic", "Metallic" },
            { "specular", "Specular" },
            { "emissive", "Emissive" },
            { "occlusion", "Occlusion" },
            { "alpha", "Alpha" },
            { "alphaClipThreshold", "Clip" },
            { "tangentOutput", "Tangent Out" },
            { "attributes", "Attributes" },
            { "worldPositionOffset", "WPO" },
            { "customizedUv0", "Custom UV0" },
            { "displacement", "Displacement" },
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return { { "texture", "Tex." }, { "uv", "UV" } };
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleCube:
        return { { "texture", "Cube" }, { "direction", "Dir" } };
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume:
        return { { "texture", "3D" }, { "uvw", "UVW" } };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray:
        return { { "texture", "Array" }, { "uv", "UV" }, { "layer", "Layer" } };
    case kb::render::RenderMaterialGraphNodeKind::Reroute:
    case kb::render::RenderMaterialGraphNodeKind::CompositeInput:
    case kb::render::RenderMaterialGraphNodeKind::CompositeOutput:
        return { { "input", "In" } };
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        return { { "input", "In" } };
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage:
        return {};
    case kb::render::RenderMaterialGraphNodeKind::FunctionOutput:
        return { { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::Add:
    case kb::render::RenderMaterialGraphNodeKind::Subtract:
    case kb::render::RenderMaterialGraphNodeKind::Multiply:
    case kb::render::RenderMaterialGraphNodeKind::Divide:
    case kb::render::RenderMaterialGraphNodeKind::Minimum:
    case kb::render::RenderMaterialGraphNodeKind::Maximum:
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
    case kb::render::RenderMaterialGraphNodeKind::Distance:
    case kb::render::RenderMaterialGraphNodeKind::Fmod:
    case kb::render::RenderMaterialGraphNodeKind::SphereMask:
        return { { "a", "A" }, { "b", "B" } };
    case kb::render::RenderMaterialGraphNodeKind::InverseLerp:
        return { { "a", "A" }, { "b", "B" }, { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::Power:
        return { { "base", "Base" }, { "exponent", "Exponent" } };
    case kb::render::RenderMaterialGraphNodeKind::OneMinus:
    case kb::render::RenderMaterialGraphNodeKind::Absolute:
    case kb::render::RenderMaterialGraphNodeKind::Saturate:
    case kb::render::RenderMaterialGraphNodeKind::Floor:
    case kb::render::RenderMaterialGraphNodeKind::Ceil:
    case kb::render::RenderMaterialGraphNodeKind::Fraction:
    case kb::render::RenderMaterialGraphNodeKind::SquareRoot:
    case kb::render::RenderMaterialGraphNodeKind::Sine:
    case kb::render::RenderMaterialGraphNodeKind::Cosine:
    case kb::render::RenderMaterialGraphNodeKind::Exponential:
    case kb::render::RenderMaterialGraphNodeKind::Exponential2:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm2:
    case kb::render::RenderMaterialGraphNodeKind::SrgbToLinear:
    case kb::render::RenderMaterialGraphNodeKind::LinearToSrgb:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm10:
    case kb::render::RenderMaterialGraphNodeKind::HsvToRgb:
    case kb::render::RenderMaterialGraphNodeKind::RgbToHsv:
    case kb::render::RenderMaterialGraphNodeKind::DeriveNormalZ:
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeX:
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeY:
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
    case kb::render::RenderMaterialGraphNodeKind::Length:
    case kb::render::RenderMaterialGraphNodeKind::BreakVector:
        return { { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::BlackBody:
        return { { "value", "Temp (K)" } };
    case kb::render::RenderMaterialGraphNodeKind::Noise:
    case kb::render::RenderMaterialGraphNodeKind::VectorNoise:
        return { { "value", "Position" } };
    case kb::render::RenderMaterialGraphNodeKind::Sobol:
        return { { "cell", "Cell" }, { "index", "Index" }, { "seed", "Seed" } };
    case kb::render::RenderMaterialGraphNodeKind::AppendVector:
        return { { "a", "XYZ" }, { "b", "W" } };
    case kb::render::RenderMaterialGraphNodeKind::ColorRamp:
        return { { "value", "Gradient" } };
    case kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask:
        return { { "value", "Mask" } };
    case kb::render::RenderMaterialGraphNodeKind::Transform:
    case kb::render::RenderMaterialGraphNodeKind::TransformPosition:
        return { { "value", "Vector" } };
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
        return { { "x", "X" }, { "y", "Y" }, { "z", "Z" }, { "w", "W" } };
    case kb::render::RenderMaterialGraphNodeKind::Step:
        return { { "edge", "Edge" }, { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
        return { { "min", "Min" }, { "max", "Max" }, { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::If:
        return { { "a", "A" }, { "b", "B" }, { "less", "Less" }, { "equal", "Equal" }, { "greater", "Greater" } };
    case kb::render::RenderMaterialGraphNodeKind::RuntimeSwitch:
        return { { "index", "Index" }, { "default", "Default" }, { "case0", "Case 0" }, { "case1", "Case 1" }, { "case2", "Case 2" }, { "case3", "Case 3" } };
    case kb::render::RenderMaterialGraphNodeKind::Desaturate:
        return { { "color", "Color" }, { "fraction", "Fraction" } };
    case kb::render::RenderMaterialGraphNodeKind::Fresnel:
        return { { "normal", "Normal" }, { "view", "View" }, { "exponent", "Exponent" }, { "base", "Base" } };
    case kb::render::RenderMaterialGraphNodeKind::Negate:
    case kb::render::RenderMaterialGraphNodeKind::Sign:
    case kb::render::RenderMaterialGraphNodeKind::Round:
    case kb::render::RenderMaterialGraphNodeKind::Truncate:
    case kb::render::RenderMaterialGraphNodeKind::Tangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcSine:
    case kb::render::RenderMaterialGraphNodeKind::ArcCosine:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcSineFast:
    case kb::render::RenderMaterialGraphNodeKind::ArcCosineFast:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangentFast:
        return { { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2Fast:
        return { { "y", "Y" }, { "x", "X" } };
    case kb::render::RenderMaterialGraphNodeKind::Clamp:
        return { { "value", "Value" }, { "min", "Min" }, { "max", "Max" } };
    case kb::render::RenderMaterialGraphNodeKind::Lerp:
        return { { "a", "A" }, { "b", "B" }, { "t", "T" } };
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        return { { "color", "Color" } };
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return {};
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
    case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return {};
    default:
        break;
    }
    // Any node not explicitly listed above falls back to the renderer's authoritative pin schema.
    return GraphPinFallback(kb::render::RenderMaterialGraphNodeInputPinNames(kind));
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>> GraphOutputPins(kb::render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
    case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
    case kb::render::RenderMaterialGraphNodeKind::Add:
    case kb::render::RenderMaterialGraphNodeKind::Subtract:
    case kb::render::RenderMaterialGraphNodeKind::Multiply:
    case kb::render::RenderMaterialGraphNodeKind::Divide:
    case kb::render::RenderMaterialGraphNodeKind::Power:
    case kb::render::RenderMaterialGraphNodeKind::OneMinus:
    case kb::render::RenderMaterialGraphNodeKind::Absolute:
    case kb::render::RenderMaterialGraphNodeKind::Minimum:
    case kb::render::RenderMaterialGraphNodeKind::Maximum:
    case kb::render::RenderMaterialGraphNodeKind::Saturate:
    case kb::render::RenderMaterialGraphNodeKind::Floor:
    case kb::render::RenderMaterialGraphNodeKind::Ceil:
    case kb::render::RenderMaterialGraphNodeKind::Fraction:
    case kb::render::RenderMaterialGraphNodeKind::SquareRoot:
    case kb::render::RenderMaterialGraphNodeKind::Sine:
    case kb::render::RenderMaterialGraphNodeKind::Cosine:
    case kb::render::RenderMaterialGraphNodeKind::Exponential:
    case kb::render::RenderMaterialGraphNodeKind::Exponential2:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm2:
    case kb::render::RenderMaterialGraphNodeKind::SrgbToLinear:
    case kb::render::RenderMaterialGraphNodeKind::LinearToSrgb:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm10:
    case kb::render::RenderMaterialGraphNodeKind::HsvToRgb:
    case kb::render::RenderMaterialGraphNodeKind::RgbToHsv:
    case kb::render::RenderMaterialGraphNodeKind::DeriveNormalZ:
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeX:
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeY:
    case kb::render::RenderMaterialGraphNodeKind::BlackBody:
    case kb::render::RenderMaterialGraphNodeKind::Noise:
    case kb::render::RenderMaterialGraphNodeKind::VectorNoise:
    case kb::render::RenderMaterialGraphNodeKind::Sobol:
    case kb::render::RenderMaterialGraphNodeKind::ColorRamp:
    case kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask:
    case kb::render::RenderMaterialGraphNodeKind::Transform:
    case kb::render::RenderMaterialGraphNodeKind::TransformPosition:
    case kb::render::RenderMaterialGraphNodeKind::Fmod:
    case kb::render::RenderMaterialGraphNodeKind::InverseLerp:
    case kb::render::RenderMaterialGraphNodeKind::SphereMask:
    case kb::render::RenderMaterialGraphNodeKind::AppendVector:
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
    case kb::render::RenderMaterialGraphNodeKind::Length:
    case kb::render::RenderMaterialGraphNodeKind::Distance:
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
    case kb::render::RenderMaterialGraphNodeKind::Step:
    case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
    case kb::render::RenderMaterialGraphNodeKind::If:
    case kb::render::RenderMaterialGraphNodeKind::RuntimeSwitch:
    case kb::render::RenderMaterialGraphNodeKind::Fresnel:
    case kb::render::RenderMaterialGraphNodeKind::Negate:
    case kb::render::RenderMaterialGraphNodeKind::Sign:
    case kb::render::RenderMaterialGraphNodeKind::Round:
    case kb::render::RenderMaterialGraphNodeKind::Truncate:
    case kb::render::RenderMaterialGraphNodeKind::Tangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcSine:
    case kb::render::RenderMaterialGraphNodeKind::ArcCosine:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2:
    case kb::render::RenderMaterialGraphNodeKind::ArcSineFast:
    case kb::render::RenderMaterialGraphNodeKind::ArcCosineFast:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangentFast:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2Fast:
    case kb::render::RenderMaterialGraphNodeKind::Clamp:
    case kb::render::RenderMaterialGraphNodeKind::Lerp:
        return { { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::Desaturate:
        return { { "color", "Color" } };
    case kb::render::RenderMaterialGraphNodeKind::BreakVector:
        return { { "x", "X" }, { "y", "Y" }, { "z", "Z" }, { "w", "W" } };
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
        return { { "xy", "XY" } };
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
        return { { "xyz", "XYZ" } };
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
        return { { "rgba", "RGBA" }, { "r", "R" }, { "g", "G" }, { "b", "B" }, { "a", "A" } };
    case kb::render::RenderMaterialGraphNodeKind::CollectionParameter:
        return {
            { "value", "Value" },
            { "scalar", "Scalar" },
            { "xyz", "XYZ" },
            { "rgba", "RGBA" },
            { "r", "R" },
            { "g", "G" },
            { "b", "B" },
            { "a", "A" },
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleCube:
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume:
    case kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray:
        return { { "color", "RGBA" }, { "r", "R" }, { "g", "G" }, { "b", "B" }, { "a", "A" } };
    case kb::render::RenderMaterialGraphNodeKind::Reroute:
    case kb::render::RenderMaterialGraphNodeKind::CompositeInput:
    case kb::render::RenderMaterialGraphNodeKind::CompositeOutput:
        return { { "output", "Out" } };
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage:
        return { { "output", "Out" } };
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        return {};
    case kb::render::RenderMaterialGraphNodeKind::FunctionInput:
        return { { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
    case kb::render::RenderMaterialGraphNodeKind::TextureObject:
        return { { "texture", "Tex." } };
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectCube:
        return { { "texture", "Cube" } };
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume:
        return { { "texture", "3D" } };
    case kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray:
        return { { "texture", "Array" } };
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        return { { "normal", "Normal" } };
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return { { "uv", "UV" } };
    case kb::render::RenderMaterialGraphNodeKind::LayerStack:
        return { { "attributes", "Attributes" } };
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return {};
    default:
        break;
    }
    return GraphPinFallback(kb::render::RenderMaterialGraphNodeOutputPinNames(kind));
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>> GraphInputPins(const kb::render::RenderMaterialGraphNode& node) {
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::CustomCode ||
        node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall) {
        return GraphPinFallback(kb::render::RenderMaterialGraphNodeInputPinNames(node));
    }
    return GraphInputPins(node.kind);
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>> GraphOutputPins(const kb::render::RenderMaterialGraphNode& node) {
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::CustomCode ||
        node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall) {
        return GraphPinFallback(kb::render::RenderMaterialGraphNodeOutputPinNames(node));
    }
    return GraphOutputPins(node.kind);
}

[[nodiscard]] std::string GraphNodeTitle(kb::render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return "Material Output";
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
        return "Value";
    case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
        return "Bool";
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
        return "Vector";
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
        return "Vector";
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
        return "RGB";
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return "Image Texture";
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleCube:
        return "Cube Texture";
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume:
        return "Volume Texture";
    case kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray:
        return "Texture Array";
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
        return "Value Parameter";
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
        return "Vector Parameter";
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
        return "RGB Parameter";
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return "Image Parameter";
    case kb::render::RenderMaterialGraphNodeKind::TextureObject:
        return "Texture Object";
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectCube:
        return "Texture Cube Object";
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume:
        return "Texture Volume Object";
    case kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray:
        return "Texture Array Object";
    case kb::render::RenderMaterialGraphNodeKind::CollectionParameter:
        return "Collection Parameter";
    case kb::render::RenderMaterialGraphNodeKind::CustomCode:
        return "Custom Code";
    case kb::render::RenderMaterialGraphNodeKind::QualitySwitch:
        return "Quality Switch";
    case kb::render::RenderMaterialGraphNodeKind::FeatureLevelSwitch:
        return "Feature Level Switch";
    case kb::render::RenderMaterialGraphNodeKind::ShadingPathSwitch:
        return "Shading Path Switch";
    case kb::render::RenderMaterialGraphNodeKind::ShaderStageSwitch:
        return "Shader Stage Switch";
    case kb::render::RenderMaterialGraphNodeKind::Reroute:
        return "Reroute";
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        return "Named In";
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage:
        return "Named Out";
    case kb::render::RenderMaterialGraphNodeKind::CompositeInput:
        return "Composite In";
    case kb::render::RenderMaterialGraphNodeKind::CompositeOutput:
        return "Composite Out";
    case kb::render::RenderMaterialGraphNodeKind::FunctionInput:
        return "Function In";
    case kb::render::RenderMaterialGraphNodeKind::FunctionOutput:
        return "Function Out";
    case kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall:
        return "Function Call";
    case kb::render::RenderMaterialGraphNodeKind::LayerStack:
        return "Layer Stack";
    case kb::render::RenderMaterialGraphNodeKind::Add:
        return "Add";
    case kb::render::RenderMaterialGraphNodeKind::Subtract:
        return "Subtract";
    case kb::render::RenderMaterialGraphNodeKind::Multiply:
        return "Multiply";
    case kb::render::RenderMaterialGraphNodeKind::Divide:
        return "Divide";
    case kb::render::RenderMaterialGraphNodeKind::Power:
        return "Power";
    case kb::render::RenderMaterialGraphNodeKind::OneMinus:
        return "One Minus";
    case kb::render::RenderMaterialGraphNodeKind::Absolute:
        return "Abs";
    case kb::render::RenderMaterialGraphNodeKind::Minimum:
        return "Min";
    case kb::render::RenderMaterialGraphNodeKind::Maximum:
        return "Max";
    case kb::render::RenderMaterialGraphNodeKind::Saturate:
        return "Saturate";
    case kb::render::RenderMaterialGraphNodeKind::Floor:
        return "Floor";
    case kb::render::RenderMaterialGraphNodeKind::Ceil:
        return "Ceil";
    case kb::render::RenderMaterialGraphNodeKind::Fraction:
        return "Frac";
    case kb::render::RenderMaterialGraphNodeKind::SquareRoot:
        return "Sqrt";
    case kb::render::RenderMaterialGraphNodeKind::Sine:
        return "Sin";
    case kb::render::RenderMaterialGraphNodeKind::Cosine:
        return "Cos";
    case kb::render::RenderMaterialGraphNodeKind::Exponential:
        return "Exp";
    case kb::render::RenderMaterialGraphNodeKind::Exponential2:
        return "Exp2";
    case kb::render::RenderMaterialGraphNodeKind::Logarithm:
        return "Log";
    case kb::render::RenderMaterialGraphNodeKind::Logarithm2:
        return "Log2";
    case kb::render::RenderMaterialGraphNodeKind::SrgbToLinear:
        return "sRGB to Linear";
    case kb::render::RenderMaterialGraphNodeKind::LinearToSrgb:
        return "Linear to sRGB";
    case kb::render::RenderMaterialGraphNodeKind::Logarithm10:
        return "Log10";
    case kb::render::RenderMaterialGraphNodeKind::HsvToRgb:
        return "HSV to RGB";
    case kb::render::RenderMaterialGraphNodeKind::RgbToHsv:
        return "RGB to HSV";
    case kb::render::RenderMaterialGraphNodeKind::DeriveNormalZ:
        return "Derive Normal Z";
    case kb::render::RenderMaterialGraphNodeKind::Fmod:
        return "Fmod";
    case kb::render::RenderMaterialGraphNodeKind::InverseLerp:
        return "Inverse Lerp";
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeX:
        return "DDX";
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeY:
        return "DDY";
    case kb::render::RenderMaterialGraphNodeKind::SphereMask:
        return "Sphere Mask";
    case kb::render::RenderMaterialGraphNodeKind::BlackBody:
        return "Black Body";
    case kb::render::RenderMaterialGraphNodeKind::Noise:
        return "Noise";
    case kb::render::RenderMaterialGraphNodeKind::VectorNoise:
        return "Vector Noise";
    case kb::render::RenderMaterialGraphNodeKind::Sobol:
        return "Sobol";
    case kb::render::RenderMaterialGraphNodeKind::AppendVector:
        return "Append Vector";
    case kb::render::RenderMaterialGraphNodeKind::ColorRamp:
        return "Color Ramp";
    case kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask:
        return "Antialiased Mask";
    case kb::render::RenderMaterialGraphNodeKind::Transform:
        return "Transform";
    case kb::render::RenderMaterialGraphNodeKind::TransformPosition:
        return "Transform Position";
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
        return "Dot Product";
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
        return "Cross Product";
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
        return "Normalize";
    case kb::render::RenderMaterialGraphNodeKind::Length:
        return "Length";
    case kb::render::RenderMaterialGraphNodeKind::Distance:
        return "Distance";
    case kb::render::RenderMaterialGraphNodeKind::BreakVector:
        return "Break Vector";
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
        return "Make Vector";
    case kb::render::RenderMaterialGraphNodeKind::Step:
        return "Step";
    case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
        return "Smooth Step";
    case kb::render::RenderMaterialGraphNodeKind::If:
        return "If";
    case kb::render::RenderMaterialGraphNodeKind::RuntimeSwitch:
        return "Switch";
    case kb::render::RenderMaterialGraphNodeKind::Desaturate:
        return "Desaturate";
    case kb::render::RenderMaterialGraphNodeKind::Fresnel:
        return "Fresnel";
    case kb::render::RenderMaterialGraphNodeKind::Negate:
        return "Negate";
    case kb::render::RenderMaterialGraphNodeKind::Sign:
        return "Sign";
    case kb::render::RenderMaterialGraphNodeKind::Round:
        return "Round";
    case kb::render::RenderMaterialGraphNodeKind::Truncate:
        return "Truncate";
    case kb::render::RenderMaterialGraphNodeKind::Tangent:
        return "Tan";
    case kb::render::RenderMaterialGraphNodeKind::ArcSine:
        return "Asin";
    case kb::render::RenderMaterialGraphNodeKind::ArcCosine:
        return "Acos";
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent:
        return "Atan";
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2:
        return "Atan2";
    case kb::render::RenderMaterialGraphNodeKind::ArcSineFast:
        return "Asin Fast";
    case kb::render::RenderMaterialGraphNodeKind::ArcCosineFast:
        return "Acos Fast";
    case kb::render::RenderMaterialGraphNodeKind::ArcTangentFast:
        return "Atan Fast";
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2Fast:
        return "Atan2 Fast";
    case kb::render::RenderMaterialGraphNodeKind::Clamp:
        return "Clamp";
    case kb::render::RenderMaterialGraphNodeKind::Lerp:
        return "Mix";
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        return "Normal Map";
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return "Texture Coordinate";
    case kb::render::RenderMaterialGraphNodeKind::TwoSidedSign:
        return "Two Sided Sign";
    default:
        break;
    }
    // Any node not explicitly named above uses the renderer's authoritative display name.
    return std::string{ kb::render::RenderMaterialGraphNodeKindName(kind) };
}

[[nodiscard]] COLORREF GraphOutputPinColor(const kb::render::RenderMaterialGraphNode& node, std::string_view pin) noexcept;

// ---------------------------------------------------------------------------------------------
// Plain 2D grid backdrop: a solid canvas fill plus a minor/major dot grid that pans and scales
// with the view.
//
// The minor dots (the bulk of the pattern -- e.g. ~2800 of them for a full graph canvas at a
// typical zoom) are baked into a small tiled pattern-brush bitmap once per minorSpacing value
// (i.e. once per zoom level, not once per frame) and stamped with a single FillRect: GDI tiles
// and positions the brush natively, so this replaces thousands of per-pixel GDI calls with one.
// Panning just moves the brush origin -- no rebuild. The sparser major dots (1/16th as many)
// stay a plain per-dot loop; that was never the bottleneck.
struct GraphGridPatternCache {
    HBITMAP tileBitmap = nullptr;
    HBRUSH brush = nullptr;
    int minorSpacing = 0;
    std::uint64_t hitCount = 0U;
    std::uint64_t rebuildCount = 0U;
};

[[nodiscard]] GraphGridPatternCache& GraphGridPattern() {
    static GraphGridPatternCache cache;
    return cache;
}

void EnsureGraphGridPattern(GraphGridPatternCache& cache, int minorSpacing) {
    if (cache.brush != nullptr && cache.minorSpacing == minorSpacing) {
        ++cache.hitCount;
        return;
    }
    ++cache.rebuildCount;
    if (cache.brush != nullptr) {
        DeleteObject(cache.brush);
        cache.brush = nullptr;
    }
    if (cache.tileBitmap != nullptr) {
        DeleteObject(cache.tileBitmap);
        cache.tileBitmap = nullptr;
    }
    HDC screenDc = GetDC(nullptr);
    HDC tileDc = CreateCompatibleDC(screenDc);
    cache.tileBitmap = CreateCompatibleBitmap(screenDc, minorSpacing, minorSpacing);
    ReleaseDC(nullptr, screenDc);
    if (tileDc == nullptr || cache.tileBitmap == nullptr) {
        if (tileDc != nullptr) {
            DeleteDC(tileDc);
        }
        return;
    }
    HGDIOBJ previousBitmap = SelectObject(tileDc, cache.tileBitmap);
    const RECT tileRect{ 0, 0, minorSpacing, minorSpacing };
    GdiDrawing::FillRectColor(tileDc, tileRect, BlenderGraphTheme::Canvas);
    SetPixelV(tileDc, 0, 0, BlenderGraphTheme::GridDotBlended);
    SelectObject(tileDc, previousBitmap);
    DeleteDC(tileDc);
    cache.brush = CreatePatternBrush(cache.tileBitmap);
    cache.minorSpacing = minorSpacing;
}

void DrawGraphGrid(HDC dc, const RECT& canvas, float zoom = 1.0F, int panX = 0, int panY = 0) {
    const int minorSpacing = std::clamp(ScaleMetric(20, zoom), 8, 80);
    const int majorSpacing = minorSpacing * 4;
    const int minorStartX = canvas.left + ((panX % minorSpacing) + minorSpacing) % minorSpacing;
    const int minorStartY = canvas.top + ((panY % minorSpacing) + minorSpacing) % minorSpacing;
    const int majorStartX = canvas.left + ((panX % majorSpacing) + majorSpacing) % majorSpacing;
    const int majorStartY = canvas.top + ((panY % majorSpacing) + majorSpacing) % majorSpacing;

    GraphGridPatternCache& patternCache = GraphGridPattern();
    EnsureGraphGridPattern(patternCache, minorSpacing);
    if (patternCache.brush != nullptr) {
        POINT previousOrigin{};
        SetBrushOrgEx(dc, minorStartX, minorStartY, &previousOrigin);
        RECT canvasRect = canvas;
        FillRect(dc, &canvasRect, patternCache.brush);
        SetBrushOrgEx(dc, previousOrigin.x, previousOrigin.y, nullptr);
    } else {
        GdiDrawing::FillRectColor(dc, canvas, BlenderGraphTheme::Canvas);
    }

    for (int x = majorStartX; x < canvas.right; x += majorSpacing) {
        for (int y = majorStartY; y < canvas.bottom; y += majorSpacing) {
            GdiDrawing::FillRectColor(dc, RECT{ x - 1, y - 1, x + 2, y + 2 }, BlenderGraphTheme::GridDotMajor);
        }
    }
}
// ---------------------------------------------------------------------------------------------

[[nodiscard]] float GraphBezierHandleDistance(POINT from, POINT to) noexcept {
    const float distanceX = static_cast<float>(std::abs(static_cast<int>(to.x - from.x)));
    const float distanceY = static_cast<float>(std::abs(static_cast<int>(to.y - from.y)));
    const float distance = std::sqrt((distanceX * distanceX) + (distanceY * distanceY));
    const float naturalHandle = std::max(distanceX * 0.5F, distance * 0.18F);
    return std::clamp(naturalHandle, 10.0F, 96.0F);
}

// Takes an already-configured Gdiplus::Graphics (see DrawGraphPin/FillRoundedRectAlpha above for
// why): a graph with many links used to construct one Gdiplus::Graphics per link, per repaint.
void DrawGraphBezier(Gdiplus::Graphics& graphics, POINT from, POINT to, COLORREF color, int width) {
    Gdiplus::Pen shadowPen(ToGdiplusColor(BlenderGraphTheme::LinkShadow, 96U), static_cast<Gdiplus::REAL>(std::max(1, width + 2)));
    Gdiplus::Pen haloPen(ToGdiplusColor(color, 50U), static_cast<Gdiplus::REAL>(std::max(1, width + 1)));
    Gdiplus::Pen pen(ToGdiplusColor(color, 232U), static_cast<Gdiplus::REAL>(std::max(1, width)));
    shadowPen.SetStartCap(Gdiplus::LineCapRound);
    shadowPen.SetEndCap(Gdiplus::LineCapRound);
    haloPen.SetStartCap(Gdiplus::LineCapRound);
    haloPen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const float dx = GraphBezierHandleDistance(from, to);
    Gdiplus::GraphicsPath path;
    path.AddBezier(
        static_cast<Gdiplus::REAL>(from.x),
        static_cast<Gdiplus::REAL>(from.y),
        static_cast<Gdiplus::REAL>(from.x) + dx,
        static_cast<Gdiplus::REAL>(from.y),
        static_cast<Gdiplus::REAL>(to.x) - dx,
        static_cast<Gdiplus::REAL>(to.y),
        static_cast<Gdiplus::REAL>(to.x),
        static_cast<Gdiplus::REAL>(to.y));
    graphics.DrawPath(&shadowPen, &path);
    graphics.DrawPath(&haloPen, &path);
    graphics.DrawPath(&pen, &path);
}

void DrawGraphLink(
    Gdiplus::Graphics& graphics,
    const RECT& fromNode,
    const RECT& toNode,
    std::string_view fromPin,
    std::string_view toPin,
    const kb::render::RenderMaterialGraphNode& fromGraphNode,
    const kb::render::RenderMaterialGraphNode& toGraphNode) {
    const std::vector<std::pair<std::string, std::string>>outputPins = GraphOutputPins(fromGraphNode);
    std::size_t outputIndex = 0U;
    for (std::size_t index = 0U; index < outputPins.size(); ++index) {
        if (outputPins[index].first == fromPin) {
            outputIndex = index;
            break;
        }
    }
    const POINT from = OutputPinPoint(fromNode, fromGraphNode.kind, outputIndex, outputPins.size());
    const POINT to = InputPinPoint(toNode, toGraphNode, toPin);
    DrawGraphBezier(
        graphics,
        from,
        to,
        outputPins.empty() ? BlenderGraphTheme::LinkFallback : GraphOutputPinColor(fromGraphNode, fromPin),
        std::clamp(ScaleMetric(3, NodeUiScale(fromNode, fromGraphNode.kind)), 1, 3));
}

[[nodiscard]] double PointSegmentDistanceSquared(double px, double py, double ax, double ay, double bx, double by) noexcept {
    const double vx = bx - ax;
    const double vy = by - ay;
    const double wx = px - ax;
    const double wy = py - ay;
    const double lengthSquared = (vx * vx) + (vy * vy);
    if (lengthSquared <= 0.0001) {
        const double dx = px - ax;
        const double dy = py - ay;
        return (dx * dx) + (dy * dy);
    }
    const double t = std::clamp(((wx * vx) + (wy * vy)) / lengthSquared, 0.0, 1.0);
    const double cx = ax + (t * vx);
    const double cy = ay + (t * vy);
    const double dx = px - cx;
    const double dy = py - cy;
    return (dx * dx) + (dy * dy);
}

[[nodiscard]] POINT GraphBezierPoint(POINT from, POINT to, double t) noexcept {
    const double dx = static_cast<double>(GraphBezierHandleDistance(from, to));
    const double x0 = static_cast<double>(from.x);
    const double y0 = static_cast<double>(from.y);
    const double x1 = x0 + dx;
    const double y1 = y0;
    const double x3 = static_cast<double>(to.x);
    const double y3 = static_cast<double>(to.y);
    const double x2 = x3 - dx;
    const double y2 = y3;
    const double u = 1.0 - t;
    const double x = (u * u * u * x0) + (3.0 * u * u * t * x1) + (3.0 * u * t * t * x2) + (t * t * t * x3);
    const double y = (u * u * u * y0) + (3.0 * u * u * t * y1) + (3.0 * u * t * t * y2) + (t * t * t * y3);
    return POINT{ static_cast<LONG>(std::lround(x)), static_cast<LONG>(std::lround(y)) };
}

[[nodiscard]] bool PointNearGraphBezier(POINT from, POINT to, int x, int y, int radius) noexcept {
    constexpr int kSegments = 24;
    POINT previous = from;
    const double threshold = static_cast<double>(radius * radius);
    for (int index = 1; index <= kSegments; ++index) {
        const POINT current = GraphBezierPoint(from, to, static_cast<double>(index) / static_cast<double>(kSegments));
        if (PointSegmentDistanceSquared(
                static_cast<double>(x),
                static_cast<double>(y),
                static_cast<double>(previous.x),
                static_cast<double>(previous.y),
                static_cast<double>(current.x),
                static_cast<double>(current.y)) <= threshold) {
            return true;
        }
        previous = current;
    }
    return false;
}

} // namespace

std::optional<MaterialEditorGraphLinkHit> MaterialEditorPanelRenderer::GraphLinkAt(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    int x,
    int y) noexcept {
    const kb::render::RenderMaterialGraphDocument defaultGraph = graph.nodes.empty()
        ? kb::render::MakeDefaultRenderMaterialGraphDocument()
        : kb::render::RenderMaterialGraphDocument{};
    const kb::render::RenderMaterialGraphDocument& graphView = graph.nodes.empty() ? defaultGraph : graph;
    const MaterialEditorPanelLayout layout = ResolveLayout(content);
    if (!MaterialEditorPanelPointInRect(layout.graphCanvas, x, y)) {
        return std::nullopt;
    }

    for (std::size_t linkIndex = graphView.links.size(); linkIndex-- > 0U;) {
        const kb::render::RenderMaterialGraphLink& link = graphView.links[linkIndex];
        const kb::render::RenderMaterialGraphNode* fromNode = kb::render::FindRenderMaterialGraphNode(graphView, link.fromNodeId);
        const kb::render::RenderMaterialGraphNode* toNode = kb::render::FindRenderMaterialGraphNode(graphView, link.toNodeId);
        if (fromNode == nullptr || toNode == nullptr) {
            continue;
        }
        const std::optional<RECT> fromRect = GraphNodeRect(content, graphView, link.fromNodeId, sceneContext, assetId);
        const std::optional<RECT> toRect = GraphNodeRect(content, graphView, link.toNodeId, sceneContext, assetId);
        if (!fromRect.has_value() || !toRect.has_value()) {
            continue;
        }

        const std::vector<std::pair<std::string, std::string>>outputPins = GraphOutputPins(*fromNode);
        std::size_t outputIndex = 0U;
        for (std::size_t index = 0U; index < outputPins.size(); ++index) {
            if (outputPins[index].first == link.fromPin) {
                outputIndex = index;
                break;
            }
        }
        const POINT from = OutputPinPoint(*fromRect, fromNode->kind, outputIndex, outputPins.size());
        const POINT to = InputPinPoint(*toRect, *toNode, link.toPin);
        const int radius = std::max(8, ScaleMetric(9, sceneContext.MaterialGraphZoom()));
        if (PointNearGraphBezier(from, to, x, y, radius)) {
            return MaterialEditorGraphLinkHit{
                .fromNodeId = link.fromNodeId,
                .fromPin = link.fromPin,
                .toNodeId = link.toNodeId,
                .toPin = link.toPin,
            };
        }
    }
    return std::nullopt;
}

namespace {

// Pin appearance only varies with (color, tinted, radius-in-pixels) -- the color comes from a
// small fixed enum palette (GraphPinTypeColor) and the radius is already an integer pixel count
// (ScaleMetric rounds it). A node graph draws one pin per input/output slot -- a Material Output
// node alone has 14 -- and each pin previously did 5 antialiased Gdiplus FillEllipse calls (shadow,
// edge, outer ring, face, shine), which dominated node-drawing time. Render the static (non-drag-
// ring) appearance once per distinct (color, tinted, radius) into a small cached Gdiplus::Bitmap
// and composite it with a single DrawImage thereafter.
constexpr int kGraphPinSpritePadding = 3;

struct GraphPinSpriteCache {
    std::unordered_map<std::uint64_t, std::unique_ptr<Gdiplus::Bitmap>> sprites;
};

[[nodiscard]] GraphPinSpriteCache& GraphPinSprites() {
    static GraphPinSpriteCache cache;
    return cache;
}

[[nodiscard]] Gdiplus::Bitmap* BuildGraphPinSprite(COLORREF color, bool tinted, int r, float insetScale) {
    HeroIconGdiplusRuntime::EnsureStarted();
    const int size = (r * 2) + (kGraphPinSpritePadding * 2);
    auto sprite = std::make_unique<Gdiplus::Bitmap>(size, size, PixelFormat32bppPARGB);
    Gdiplus::Graphics graphics(sprite.get());
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

    const COLORREF edge = RGB(9, 9, 9);
    const COLORREF outer = ScaleColor(color, tinted ? 0.64F : 0.78F);
    const COLORREF face = tinted ? color : ScaleColor(color, 1.06F);
    const float diameter = static_cast<float>(r * 2);
    const Gdiplus::RectF outerRect{
        static_cast<float>(kGraphPinSpritePadding),
        static_cast<float>(kGraphPinSpritePadding),
        diameter,
        diameter,
    };
    const float inset = std::max(1.0F, insetScale);
    const Gdiplus::RectF innerRect{
        outerRect.X + inset,
        outerRect.Y + inset,
        std::max(1.0F, outerRect.Width - (inset * 2.0F)),
        std::max(1.0F, outerRect.Height - (inset * 2.0F)),
    };
    Gdiplus::SolidBrush shadowBrush(ToGdiplusColor(RGB(0, 0, 0), 84U));
    Gdiplus::SolidBrush edgeBrush(ToGdiplusColor(edge));
    Gdiplus::SolidBrush outerBrush(ToGdiplusColor(outer));
    Gdiplus::SolidBrush faceBrush(ToGdiplusColor(face));
    Gdiplus::SolidBrush shineBrush(ToGdiplusColor(RGB(255, 255, 255), 40U));
    Gdiplus::RectF shadowRect = outerRect;
    shadowRect.X += std::max(1.0F, insetScale);
    shadowRect.Y += std::max(1.0F, insetScale);
    graphics.FillEllipse(&shadowBrush, shadowRect);
    graphics.FillEllipse(&edgeBrush, outerRect);
    graphics.FillEllipse(&outerBrush, innerRect);
    const float coreInset = std::max(1.0F, inset * 1.35F);
    const Gdiplus::RectF coreRect{
        outerRect.X + coreInset,
        outerRect.Y + coreInset,
        std::max(1.0F, outerRect.Width - (coreInset * 2.0F)),
        std::max(1.0F, outerRect.Height - (coreInset * 2.0F)),
    };
    graphics.FillEllipse(&faceBrush, coreRect);
    graphics.FillEllipse(
        &shineBrush,
        Gdiplus::RectF{
            outerRect.X + coreInset,
            outerRect.Y + coreInset,
            std::max(1.0F, coreRect.Width * 0.72F),
            std::max(1.0F, coreRect.Height * 0.42F),
        });
    return sprite.release();
}

[[nodiscard]] Gdiplus::Bitmap& GraphPinSprite(COLORREF color, bool tinted, int r, float insetScale) {
    GraphPinSpriteCache& cache = GraphPinSprites();
    const std::uint64_t key = (static_cast<std::uint64_t>(color) << 24U) |
        (static_cast<std::uint64_t>(tinted ? 1U : 0U) << 16U) |
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(r) & 0xFFFFU);
    auto found = cache.sprites.find(key);
    if (found == cache.sprites.end()) {
        found = cache.sprites.emplace(key, std::unique_ptr<Gdiplus::Bitmap>(BuildGraphPinSprite(color, tinted, r, insetScale))).first;
    }
    return *found->second;
}

void DrawGraphPin(
    Gdiplus::Graphics& graphics,
    POINT point,
    COLORREF color,
    float scale,
    bool tinted = false,
    MaterialEditorGraphPinDragState dragState = MaterialEditorGraphPinDragState::None) {
    const int r = std::max(3, ScaleMetric(kGraphNodePinRadius, scale));
    const float inset = std::max(1.0F, scale);
    Gdiplus::Bitmap& sprite = GraphPinSprite(color, tinted, r, inset);
    const float spriteSize = static_cast<float>((r * 2) + (kGraphPinSpritePadding * 2));
    graphics.DrawImage(
        &sprite,
        Gdiplus::RectF{
            static_cast<float>(point.x - r - kGraphPinSpritePadding),
            static_cast<float>(point.y - r - kGraphPinSpritePadding),
            spriteSize,
            spriteSize,
        });
    const Gdiplus::RectF outerRect{
        static_cast<float>(point.x - r),
        static_cast<float>(point.y - r),
        static_cast<float>(r * 2),
        static_cast<float>(r * 2),
    };
    if (dragState != MaterialEditorGraphPinDragState::None) {
        const COLORREF ringColor = dragState == MaterialEditorGraphPinDragState::Compatible
            ? RGB(92, 210, 126)
            : (dragState == MaterialEditorGraphPinDragState::Incompatible ? RGB(231, 88, 88) : RGB(118, 174, 255));
        Gdiplus::Pen ringPen(ToGdiplusColor(ringColor, dragState == MaterialEditorGraphPinDragState::Source ? 190U : 220U), std::max<Gdiplus::REAL>(2.0F, scale * 2.0F));
        const float ringInset = std::max(1.0F, scale * 1.2F);
        Gdiplus::RectF ringRect{
            outerRect.X - ringInset,
            outerRect.Y - ringInset,
            outerRect.Width + (ringInset * 2.0F),
            outerRect.Height + (ringInset * 2.0F),
        };
        graphics.DrawEllipse(&ringPen, ringRect);
    }
}

[[nodiscard]] MaterialEditorGraphPinDragState GraphPinDragStateForNode(
    const kb::render::RenderMaterialGraphDocument& graph,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialGraphNode& node,
    std::string_view pin,
    bool outputPin) noexcept {
    if (!sceneContext.HasMaterialGraphPinConnection() ||
        sceneContext.MaterialGraphPinConnectionAssetId() != assetId) {
        return MaterialEditorGraphPinDragState::None;
    }
    return MaterialEditorPanelRenderer::GraphPinDragState(
        graph,
        sceneContext.MaterialGraphPinConnectionNodeId(),
        sceneContext.MaterialGraphPinConnectionPin(),
        sceneContext.MaterialGraphPinConnectionIsOutput(),
        node.id,
        pin,
        outputPin);
}

[[nodiscard]] COLORREF GraphPinTypeColor(kb::render::RenderMaterialGraphPinType type) noexcept {
    switch (type) {
    case kb::render::RenderMaterialGraphPinType::Float:
        return RGB(178, 178, 178);
    case kb::render::RenderMaterialGraphPinType::Float2:
        return RGB(91, 157, 216);
    case kb::render::RenderMaterialGraphPinType::Float3:
        return RGB(82, 181, 159);
    case kb::render::RenderMaterialGraphPinType::Float4:
        return RGB(218, 151, 76);
    case kb::render::RenderMaterialGraphPinType::Color:
        return RGB(220, 170, 48);
    case kb::render::RenderMaterialGraphPinType::Texture2D:
    case kb::render::RenderMaterialGraphPinType::TextureCube:
    case kb::render::RenderMaterialGraphPinType::Texture3D:
    case kb::render::RenderMaterialGraphPinType::Texture2DArray:
        return RGB(184, 143, 214);
    case kb::render::RenderMaterialGraphPinType::Sampler:
        return RGB(150, 133, 220);
    case kb::render::RenderMaterialGraphPinType::Normal:
        return RGB(95, 165, 223);
    case kb::render::RenderMaterialGraphPinType::Bool:
        return RGB(118, 187, 102);
    case kb::render::RenderMaterialGraphPinType::MaterialAttributes:
        return RGB(218, 112, 176);
    case kb::render::RenderMaterialGraphPinType::Unknown:
        return RGB(176, 176, 176);
    }
    return RGB(176, 176, 176);
}

[[nodiscard]] COLORREF GraphInputPinColor(const kb::render::RenderMaterialGraphNode& node, std::string_view pin) noexcept {
    return GraphPinTypeColor(kb::render::RenderMaterialGraphPinDataType(node, pin, false));
}

[[nodiscard]] COLORREF GraphOutputPinLabelColor(kb::render::RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    if (!MaterialEditorPanelIsTextureSamplePreviewNode(kind)) {
        return BlenderGraphTheme::TextMuted;
    }
    static_cast<void>(pin);
    return BlenderGraphTheme::Text;
}

[[nodiscard]] COLORREF GraphOutputPinColor(kb::render::RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    return GraphPinTypeColor(kb::render::RenderMaterialGraphPinDataType(kind, pin, true));
}

[[nodiscard]] COLORREF GraphOutputPinColor(const kb::render::RenderMaterialGraphNode& node, std::string_view pin) noexcept {
    return GraphPinTypeColor(kb::render::RenderMaterialGraphPinDataType(node, pin, true));
}

[[nodiscard]] bool GraphOutputPinTinted(kb::render::RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    return (MaterialEditorPanelIsTextureSamplePreviewNode(kind) ||
            kind == kb::render::RenderMaterialGraphNodeKind::CollectionParameter) &&
        (pin == "r" || pin == "g" || pin == "b");
}

[[nodiscard]] COLORREF GraphNodeHeaderColor(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    // Every node shares the same deep-space-blue header now -- the per-category tinting Blender uses
    // was replaced with one uniform cosmic look, per the redesign brief.
    static_cast<void>(kind);
    return BlenderGraphTheme::NodeHeader;
}

void DrawGraphNodeFrame(HDC dc, Gdiplus::Graphics& graphics, const RECT& rect, COLORREF body, float scale) {
    const int cornerDiameter = ScaleMetric(kGraphNodeCornerDiameter, scale);
    const int outerSpread = ScaleMetric(7, scale);
    const int middleSpread = ScaleMetric(4, scale);
    const int contactDrop = ScaleMetric(5, scale);
    const int contactSpread = ScaleMetric(3, scale);

    FillRoundedRectAlpha(
        graphics,
        RECT{ rect.left - outerSpread, rect.top - outerSpread, rect.right + outerSpread, rect.bottom + outerSpread },
        BlenderGraphTheme::NodeShadow,
        26U,
        cornerDiameter + outerSpread);
    FillRoundedRectAlpha(
        graphics,
        RECT{ rect.left - middleSpread, rect.top - middleSpread, rect.right + middleSpread, rect.bottom + middleSpread },
        BlenderGraphTheme::NodeShadow,
        42U,
        cornerDiameter + middleSpread);
    FillRoundedRectAlpha(
        graphics,
        RECT{ rect.left - contactSpread, rect.top + contactDrop, rect.right + contactSpread, rect.bottom + contactDrop + contactSpread },
        BlenderGraphTheme::NodeShadow,
        72U,
        cornerDiameter + contactSpread);
    FillRoundedRect(dc, rect, body, ScaleMetric(kGraphNodeCornerDiameter, scale));
}

[[nodiscard]] COLORREF GraphCommentColor(std::uint32_t color) noexcept {
    if (color == 0U) {
        return RGB(74, 99, 133);
    }
    return RGB((color >> 16U) & 0xFFU, (color >> 8U) & 0xFFU, color & 0xFFU);
}

void DrawGraphCommentBox(HDC dc, const RECT& rect, const kb::render::RenderMaterialGraphCommentBox& comment, bool selected) {
    const COLORREF color = GraphCommentColor(comment.color);
    const float scale = std::clamp(
        static_cast<float>(RectWidth(rect)) / static_cast<float>(std::max(1, comment.width)),
        0.45F,
        1.8F);
    const int cornerDiameter = std::max(4, ScaleMetric(7, scale));
    FillRoundedRectAlpha(dc, rect, color, selected ? 78U : 54U, cornerDiameter);
    GdiDrawing::FillRectAlpha(dc, RECT{ rect.left + 2, rect.top + 2, rect.right - 2, std::min(rect.bottom - 2, rect.top + ScaleMetric(28, scale)) }, color, selected ? 92 : 68);
    StrokeRoundedRect(dc, rect, selected ? BlenderGraphTheme::NodeOutlineSelected : ScaleColor(color, 1.28F), cornerDiameter, selected ? 2 : 1);
    const RECT title{
        rect.left + ScaleMetric(10, scale),
        rect.top + ScaleMetric(3, scale),
        rect.right - ScaleMetric(10, scale),
        std::min(rect.bottom, rect.top + ScaleMetric(26, scale)),
    };
    const std::string label = comment.text.empty() ? std::string{ "Comment" } : comment.text;
    DrawGraphText(dc, title, label.c_str(), RGB(239, 244, 249), ScaleMetric(kGraphPinFontSize, scale), FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawGraphCompositeBox(HDC dc, const RECT& rect, const kb::render::RenderMaterialGraphCompositeSubgraph& composite) {
    const COLORREF color = GraphCommentColor(composite.color);
    const float scale = std::clamp(
        static_cast<float>(RectWidth(rect)) / static_cast<float>(std::max(1, composite.width)),
        0.45F,
        1.8F);
    const int cornerDiameter = std::max(4, ScaleMetric(7, scale));
    FillRoundedRectAlpha(dc, rect, color, composite.collapsed ? 86U : 44U, cornerDiameter);
    GdiDrawing::FillRectAlpha(dc, RECT{ rect.left + 2, rect.top + 2, rect.right - 2, std::min(rect.bottom - 2, rect.top + ScaleMetric(30, scale)) }, color, composite.collapsed ? 108 : 72);
    StrokeRoundedRect(dc, rect, composite.collapsed ? ScaleColor(color, 1.45F) : ScaleColor(color, 1.18F), cornerDiameter, composite.collapsed ? 2 : 1);
    const RECT title{
        rect.left + ScaleMetric(10, scale),
        rect.top + ScaleMetric(3, scale),
        rect.right - ScaleMetric(10, scale),
        std::min(rect.bottom, rect.top + ScaleMetric(28, scale)),
    };
    const std::string label = composite.name.empty() ? std::string{ "Composite" } : composite.name;
    DrawGraphText(dc, title, label.c_str(), RGB(239, 244, 249), ScaleMetric(kGraphPinFontSize, scale), FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const RECT status{
        rect.left + ScaleMetric(10, scale),
        std::min(rect.bottom - ScaleMetric(24, scale), rect.top + ScaleMetric(34, scale)),
        rect.right - ScaleMetric(10, scale),
        rect.bottom - ScaleMetric(4, scale),
    };
    const std::string summary = std::to_string(composite.nodeIds.size()) + (composite.nodeIds.size() == 1U ? " node" : " nodes");
    DrawGraphText(dc, status, summary.c_str(), RGB(205, 218, 226), ScaleMetric(kGraphPinFontSize, scale), FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

[[nodiscard]] bool GraphNodeHiddenByCollapsedComposite(
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId) noexcept {
    for (const kb::render::RenderMaterialGraphCompositeSubgraph& composite : graph.composites) {
        if (!composite.collapsed) {
            continue;
        }
        if (std::find(composite.nodeIds.begin(), composite.nodeIds.end(), nodeId) != composite.nodeIds.end()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string TextureSampleStableId(const kb::render::RenderMaterialGraphNode& node) {
    if (!node.parameter.stableId.empty()) {
        return node.parameter.stableId;
    }
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture) {
        return "texture" + std::to_string(node.id);
    }
    if (MaterialEditorPanelIsTextureObjectPreviewNode(node.kind)) {
        return "textureObject" + std::to_string(node.id);
    }
    return "textureSample" + std::to_string(node.id);
}

[[nodiscard]] const kb::render::RenderMaterialGraphNode* TextureValueNodeForDisplay(
    const kb::render::RenderMaterialAssetData& material,
    const kb::render::RenderMaterialGraphNode& node) noexcept {
    if (MaterialEditorPanelIsTextureObjectPreviewNode(node.kind)) {
        return &node;
    }
    if (!MaterialEditorPanelIsTextureSamplePreviewNode(node.kind)) {
        return nullptr;
    }
    for (const kb::render::RenderMaterialGraphLink& link : material.graph.links) {
        if (link.toNodeId != node.id || link.toPin != "texture") {
            continue;
        }
        const kb::render::RenderMaterialGraphNode* source = kb::render::FindRenderMaterialGraphNode(material.graph, link.fromNodeId);
        if (source != nullptr && MaterialEditorPanelIsTextureObjectPreviewNode(source->kind) && link.fromPin == "texture") {
            return source;
        }
    }
    return &node;
}

[[nodiscard]] kb::assets::AssetId TextureNodeAssetId(const kb::render::RenderMaterialAssetData* material, const kb::render::RenderMaterialGraphNode& node) {
    if (material == nullptr) {
        return {};
    }
    const kb::render::RenderMaterialGraphNode* textureNode = TextureValueNodeForDisplay(*material, node);
    if (textureNode == nullptr) {
        return {};
    }
    const std::string stableId = TextureSampleStableId(*textureNode);
    for (const kb::render::RenderMaterialGraphParameterValue& value : material->graphParameterValues) {
        if (value.stableId == stableId && value.type == kb::render::RenderMaterialParameterType::Texture) {
            return kb::assets::AssetId{ value.assetId };
        }
    }
    return {};
}

[[nodiscard]] const kb::assets::AssetMetadata* TextureNodeMetadata(
    const kb::render::RenderMaterialAssetData* material,
    const kb::render::RenderMaterialGraphNode& node,
    const EditorSceneContext& sceneContext) {
    const kb::assets::AssetId assetId = TextureNodeAssetId(material, node);
    return assetId.IsValid()
        ? sceneContext.Scene().Assets().Manager().Registry().Find(assetId)
        : nullptr;
}

[[nodiscard]] std::string TextureNodeDisplayLabel(const kb::assets::AssetMetadata* metadata) {
    if (metadata == nullptr) {
        return "No texture assigned";
    }
    if (!metadata->name.empty()) {
        return metadata->name;
    }
    if (!metadata->virtualPath.empty()) {
        return metadata->virtualPath.stem().string();
    }
    return "Texture";
}

void DrawTexturePreviewBlock(
    HDC dc,
    const RECT& nodeRect,
    const RECT& preview,
    const kb::render::RenderMaterialGraphNode& node,
    const kb::render::RenderMaterialAssetData* material,
    const EditorSceneContext& sceneContext) {
    const float scale = NodeUiScale(nodeRect, node.kind);
    FillRoundedRect(dc, preview, RGB(24, 24, 24), std::max(4, ScaleMetric(5, NodeUiScale(nodeRect, node.kind))));
    const kb::assets::AssetMetadata* metadata = TextureNodeMetadata(material, node, sceneContext);
    if (metadata != nullptr) {
        if (const EditorTexturePreviewImage* image = EditorTexturePreviewService::PreviewFor(*metadata); image != nullptr) {
            EditorTexturePreviewService::DrawContain(dc, preview, *image, true);
        } else {
            StrokeRoundedRect(dc, preview, RGB(66, 66, 66), std::max(4, ScaleMetric(5, NodeUiScale(nodeRect, node.kind))));
        }
    } else {
        StrokeRoundedRect(dc, preview, RGB(66, 66, 66), std::max(4, ScaleMetric(5, NodeUiScale(nodeRect, node.kind))));
        DrawGraphText(dc, preview, "No texture", BlenderGraphTheme::TextMuted, ScaleMetric(kGraphPinFontSize, scale), FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void DrawTexturePickerFooter(
    HDC dc,
    const RECT& nodeRect,
    const RECT& footer,
    const kb::render::RenderMaterialGraphNode& node,
    const kb::render::RenderMaterialAssetData* material,
    const EditorSceneContext& sceneContext) {
    const float scale = NodeUiScale(nodeRect, node.kind);
    const kb::assets::AssetMetadata* metadata = TextureNodeMetadata(material, node, sceneContext);
    FillRoundedRect(dc, footer, RGB(31, 35, 41), std::max(3, ScaleMetric(4, scale)));
    StrokeRoundedRect(dc, footer, RGB(63, 75, 91), std::max(3, ScaleMetric(4, scale)));
    const std::string label = metadata == nullptr
        ? (RectWidth(footer) < ScaleMetric(118, scale) ? std::string{ "Choose..." } : std::string{ "Choose texture..." })
        : TextureNodeDisplayLabel(metadata);
    DrawGraphText(
        dc,
        RECT{ footer.left + ScaleMetric(8, scale), footer.top, footer.right - ScaleMetric(8, scale), footer.bottom },
        label.c_str(),
        metadata == nullptr ? RGB(168, 184, 205) : RGB(222, 232, 244),
        ScaleMetric(kGraphPinFontSize, scale),
        FW_NORMAL,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawTextureSamplePreview(HDC dc, const RECT& nodeRect, const kb::render::RenderMaterialGraphNode& node, const kb::render::RenderMaterialAssetData* material, const EditorSceneContext& sceneContext) {
    if (!MaterialEditorPanelIsTextureSamplePreviewNode(node.kind)) {
        return;
    }

    DrawTexturePreviewBlock(dc, nodeRect, MaterialEditorPanelTextureSamplePreviewRect(nodeRect), node, material, sceneContext);
    DrawTexturePickerFooter(dc, nodeRect, MaterialEditorPanelTextureSamplePickerRect(nodeRect), node, material, sceneContext);
}

void DrawTextureParameterValue(HDC dc, const RECT& nodeRect, const kb::render::RenderMaterialGraphNode& node, const kb::render::RenderMaterialAssetData* material, const EditorSceneContext& sceneContext) {
    if (!MaterialEditorPanelIsTextureObjectPreviewNode(node.kind)) {
        return;
    }

    const float scale = NodeUiScale(nodeRect, node.kind);
    const RECT valueRect = MaterialEditorPanelTextureParameterRect(nodeRect);
    FillRoundedRect(dc, valueRect, RGB(31, 31, 31), std::max(4, ScaleMetric(6, scale)));
    StrokeRoundedRect(dc, valueRect, RGB(18, 18, 18), std::max(4, ScaleMetric(6, scale)));
    const RECT preview{
        valueRect.left + ScaleMetric(8, scale),
        valueRect.top + ScaleMetric(8, scale),
        valueRect.right - ScaleMetric(8, scale),
        valueRect.bottom - ScaleMetric(32, scale),
    };
    DrawTexturePreviewBlock(dc, nodeRect, preview, node, material, sceneContext);
    DrawTexturePickerFooter(
        dc,
        nodeRect,
        RECT{
            preview.left,
            preview.bottom + ScaleMetric(5, scale),
            preview.right,
            valueRect.bottom - ScaleMetric(7, scale),
        },
        node,
        material,
        sceneContext);
}

std::array<std::string, 4U> ConstantComponentTexts(
    const MaterialEditorParameterValue& value,
    std::string_view editBuffer,
    std::size_t componentCount,
    bool editing) {
    std::array<std::string, 4U> texts{};
    for (std::size_t index = 0U; index < componentCount && index < texts.size(); ++index) {
        texts[index] = MaterialEditorPanelFloat(value.numbers[index]);
    }
    if (!editing) {
        return texts;
    }

    std::istringstream input{ std::string{ editBuffer } };
    std::size_t parsedCount = 0U;
    for (; parsedCount < componentCount && parsedCount < texts.size(); ++parsedCount) {
        float parsed = 0.0F;
        if (!(input >> parsed)) {
            break;
        }
        texts[parsedCount] = MaterialEditorPanelFloat(parsed);
    }
    if (parsedCount == 0U) {
        texts[0U] = std::string{ editBuffer } + "|";
    } else {
        texts[std::min(parsedCount - 1U, texts.size() - 1U)] += "|";
    }
    return texts;
}

void DrawGraphValueSliderField(
    HDC dc,
    const RECT& fieldRect,
    const RECT& textRect,
    const char* text,
    float value,
    bool editing,
    float scale) {
    const int radius = std::max(3, ScaleMetric(5, scale));
    FillRoundedRect(dc, fieldRect, BlenderGraphTheme::Field, radius);
    StrokeRoundedRect(dc, fieldRect, editing ? BlenderGraphTheme::FieldFocus : BlenderGraphTheme::FieldBorder, radius, editing ? 2 : 1);

    const RECT fillBounds{
        fieldRect.left + 1,
        fieldRect.top + 1,
        fieldRect.right - 1,
        fieldRect.bottom - 1,
    };
    const float normalized = std::clamp(value, 0.0F, 1.0F);
    const int fillWidth = static_cast<int>(std::round(static_cast<float>(std::max(0, static_cast<int>(fillBounds.right - fillBounds.left))) * normalized));
    if (fillWidth > 0) {
        const RECT fillRect{ fillBounds.left, fillBounds.top, fillBounds.left + fillWidth, fillBounds.bottom };
        FillRoundedRect(dc, fillRect, editing ? BlenderGraphTheme::SliderFillFocus : BlenderGraphTheme::SliderFill, radius);
    }
    GdiDrawing::FillRectAlpha(dc, RECT{ fillBounds.left + 2, fillBounds.top + 1, fillBounds.right - 2, fillBounds.top + std::max(2, ScaleMetric(2, scale)) }, RGB(255, 255, 255), 18);
    DrawGraphText(
        dc,
        textRect,
        text,
        RGB(238, 238, 238),
        ScaleMetric(kGraphPinFontSize, scale),
        FW_NORMAL,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

[[nodiscard]] int ColorByte(float value) noexcept {
    return std::clamp(static_cast<int>(std::round(std::clamp(value, 0.0F, 1.0F) * 255.0F)), 0, 255);
}

[[nodiscard]] COLORREF ColorRef(const MaterialEditorParameterValue& value) noexcept {
    return RGB(ColorByte(value.numbers[0]), ColorByte(value.numbers[1]), ColorByte(value.numbers[2]));
}

[[nodiscard]] std::string ColorHexText(const MaterialEditorParameterValue& value, bool alpha) {
    std::ostringstream output;
    output << "#"
           << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << ColorByte(value.numbers[0])
           << std::setw(2) << ColorByte(value.numbers[1])
           << std::setw(2) << ColorByte(value.numbers[2]);
    if (alpha) {
        output << std::setw(2) << ColorByte(value.numbers[3]);
    }
    return output.str();
}

void DrawColorCheckerboard(HDC dc, const RECT& rect, int cellSize) {
    const int safeCell = std::max(2, cellSize);
    for (int y = rect.top; y < rect.bottom; y += safeCell) {
        for (int x = rect.left; x < rect.right; x += safeCell) {
            const bool light = (((x - rect.left) / safeCell) + ((y - rect.top) / safeCell)) % 2 == 0;
            GdiDrawing::FillRectColor(
                dc,
                RECT{ x, y, std::min<LONG>(rect.right, x + safeCell), std::min<LONG>(rect.bottom, y + safeCell) },
                light ? RGB(78, 83, 90) : RGB(42, 46, 52));
        }
    }
}

void DrawColorSwatch(HDC dc, const RECT& rect, const MaterialEditorParameterValue& value, float scale, bool selected = false) {
    const int radius = std::max(4, ScaleMetric(6, scale));
    DrawColorCheckerboard(dc, rect, std::max(3, ScaleMetric(5, scale)));
    const BYTE alpha = static_cast<BYTE>(ColorByte(value.numbers[3]));
    GdiDrawing::FillRectAlpha(dc, rect, ColorRef(value), alpha);
    StrokeRoundedRect(dc, rect, selected ? RGB(126, 177, 235) : RGB(14, 17, 21), radius, selected ? 2 : 1);
    GdiDrawing::FillRectAlpha(dc, RECT{ rect.left + 1, rect.top + 1, rect.right - 1, std::min(rect.bottom, rect.top + ScaleMetric(5, scale)) }, RGB(255, 255, 255), 28);
}

void DrawColorWatcherPalette(
    HDC dc,
    const RECT& nodeRect,
    kb::render::RenderMaterialGraphNodeKind kind,
    const MaterialEditorParameterValue& current,
    float scale) {
    for (std::size_t chipIndex = 0U; chipIndex < 7U; ++chipIndex) {
        const RECT chip = MaterialEditorPanelColorWatcherPaletteChipRect(nodeRect, kind, chipIndex);
        if (chip.right <= chip.left || chip.bottom <= chip.top) {
            continue;
        }
        DrawColorSwatch(dc, chip, MaterialEditorPanelColorWatcherPaletteValue(chipIndex, current), scale, chipIndex == 0U);
    }
}

void DrawColorWatcherChannels(
    HDC dc,
    const RECT& nodeRect,
    kb::render::RenderMaterialGraphNodeKind kind,
    const MaterialEditorParameterValue& value,
    std::string_view editBuffer,
    std::size_t componentCount,
    bool editing,
    float scale) {
    static constexpr std::array<const char*, 4U> kLabels{ "R", "G", "B", "A" };
    const std::array<std::string, 4U> texts = ConstantComponentTexts(value, editBuffer, componentCount, editing);
    for (std::size_t index = 0U; index < componentCount; ++index) {
        const RECT labelRect = MaterialEditorPanelColorWatcherChannelLabelRect(nodeRect, kind, index, componentCount);
        const RECT fieldRect = MaterialEditorPanelColorWatcherChannelRect(nodeRect, kind, index, componentCount);
        DrawGraphText(
            dc,
            labelRect,
            kLabels[index],
            BlenderGraphTheme::TextMuted,
            ScaleMetric(kGraphPinFontSize, scale),
            FW_NORMAL,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawGraphValueSliderField(
            dc,
            fieldRect,
            RECT{
                fieldRect.left + ScaleMetric(4, scale),
                fieldRect.top,
                fieldRect.right - ScaleMetric(4, scale),
                fieldRect.bottom,
            },
            texts[index].c_str(),
            value.numbers[index],
            editing,
            scale);
    }
}

void DrawColorWatcher(
    HDC dc,
    const RECT& nodeRect,
    kb::render::RenderMaterialGraphNodeKind kind,
    const MaterialEditorParameterValue& value,
    std::string_view editBuffer,
    std::size_t componentCount,
    bool editing,
    float scale) {
    const RECT watcher = MaterialEditorPanelColorWatcherRect(nodeRect, kind);
    FillRoundedRect(dc, watcher, RGB(25, 28, 33), std::max(5, ScaleMetric(7, scale)));
    StrokeRoundedRect(dc, watcher, RGB(58, 68, 82), std::max(5, ScaleMetric(7, scale)));

    const RECT swatch = MaterialEditorPanelColorWatcherSwatchRect(nodeRect, kind);
    DrawColorSwatch(dc, swatch, value, scale);

    const bool hasAlpha = componentCount >= 4U || kind == kb::render::RenderMaterialGraphNodeKind::ParameterColor;
    const std::string hex = ColorHexText(value, hasAlpha);
    const std::string rgbText = "RGB " + MaterialEditorPanelFloat(value.numbers[0]) + " " +
        MaterialEditorPanelFloat(value.numbers[1]) + " " + MaterialEditorPanelFloat(value.numbers[2]);
    const RECT textRect = MaterialEditorPanelColorWatcherTextRect(nodeRect, kind);
    DrawGraphText(
        dc,
        textRect,
        hex.c_str(),
        RGB(232, 239, 248),
        ScaleMetric(kGraphPinFontSize, scale),
        FW_SEMIBOLD,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawGraphText(
        dc,
        RECT{ textRect.left, textRect.bottom - 1, textRect.right, textRect.bottom + ScaleMetric(14, scale) },
        rgbText.c_str(),
        BlenderGraphTheme::TextMuted,
        ScaleMetric(8, scale),
        FW_NORMAL,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawColorWatcherPalette(dc, nodeRect, kind, value, scale);
    if (componentCount > 0U) {
        DrawColorWatcherChannels(dc, nodeRect, kind, value, editBuffer, componentCount, editing, scale);
    }
}

void DrawConstantVectorFields(
    HDC dc,
    const RECT& nodeRect,
    const MaterialEditorParameterValue& value,
    std::string_view editBuffer,
    std::size_t componentCount,
    const char* const* labels,
    bool editing,
    float scale) {
    const std::array<std::string, 4U> texts = ConstantComponentTexts(value, editBuffer, componentCount, editing);
    for (std::size_t index = 0U; index < componentCount; ++index) {
        const RECT labelRect = MaterialEditorPanelConstantVectorLabelRect(nodeRect, index, componentCount);
        const RECT fieldRect = MaterialEditorPanelConstantVectorFieldRect(nodeRect, index, componentCount);
        DrawGraphText(
            dc,
            labelRect,
            labels[index],
            BlenderGraphTheme::TextMuted,
            ScaleMetric(kGraphPinFontSize, scale),
            FW_NORMAL,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawGraphValueSliderField(
            dc,
            fieldRect,
            RECT{
                fieldRect.left + ScaleMetric(8, scale),
                fieldRect.top,
                fieldRect.right - ScaleMetric(8, scale),
                fieldRect.bottom,
            },
            texts[index].c_str(),
            value.numbers[index],
            editing,
            scale);
    }
}

void DrawConstantValue(HDC dc, const RECT& nodeRect, const kb::render::RenderMaterialGraphNode& node, const EditorSceneContext& sceneContext) {
    if (!MaterialEditorPanelIsConstantNode(node.kind)) {
        return;
    }

    const float scale = NodeUiScale(nodeRect, node.kind);
    const RECT valueRect = MaterialEditorPanelConstantValueRect(nodeRect);
    const bool editing = sceneContext.MaterialEditor().IsGraphConstantInlineEditing(node.id);

    const MaterialEditorParameterValue value = MaterialEditorPanelConstantParameterValue(node.kind, node.parameter.defaultValueHint);
    const std::string valueText = editing
        ? std::string{ sceneContext.MaterialEditor().GraphConstantInlineEditBuffer() } + "|"
        : MaterialEditorPanelParameterValueText(value);
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantScalar) {
        DrawGraphValueSliderField(
            dc,
            valueRect,
            RECT{ valueRect.left + ScaleMetric(13, scale), valueRect.top, valueRect.right - ScaleMetric(10, scale), valueRect.bottom },
            valueText.c_str(),
            value.numbers[0],
            editing,
            scale);
        return;
    }
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector2) {
        const char* labels[] = { "X", "Y" };
        DrawConstantVectorFields(
            dc,
            nodeRect,
            value,
            sceneContext.MaterialEditor().GraphConstantInlineEditBuffer(),
            2U,
            labels,
            editing,
            scale);
        return;
    }
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector) {
        DrawColorWatcher(
            dc,
            nodeRect,
            node.kind,
            value,
            sceneContext.MaterialEditor().GraphConstantInlineEditBuffer(),
            3U,
            editing,
            scale);
        return;
    }
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor) {
        DrawColorWatcher(
            dc,
            nodeRect,
            node.kind,
            value,
            sceneContext.MaterialEditor().GraphConstantInlineEditBuffer(),
            4U,
            editing,
            scale);
    }
}

void DrawParameterColorValue(HDC dc, const RECT& nodeRect, const kb::render::RenderMaterialGraphNode& node, const kb::render::RenderMaterialAssetData* material) {
    if (node.kind != kb::render::RenderMaterialGraphNodeKind::ParameterColor || material == nullptr) {
        return;
    }
    const float scale = NodeUiScale(nodeRect, node.kind);
    DrawColorWatcher(
        dc,
        nodeRect,
        node.kind,
        MaterialEditorPanelParameterColorValueForNode(*material, node),
        {},
        0U,
        false,
        scale);
}

[[nodiscard]] MaterialEditorPanelColorRampStopModel ColorRampSample(
    const std::vector<MaterialEditorPanelColorRampStopModel>& stops,
    float position) {
    if (stops.empty()) {
        return MaterialEditorPanelColorRampStopModel{};
    }
    const float t = std::clamp(position, 0.0F, 1.0F);
    const MaterialEditorPanelColorRampStopModel* leftStop = &stops.front();
    const MaterialEditorPanelColorRampStopModel* rightStop = &stops.back();
    for (std::size_t index = 1U; index < stops.size(); ++index) {
        if (stops[index].position >= t) {
            leftStop = &stops[index - 1U];
            rightStop = &stops[index];
            break;
        }
    }
    const float span = std::max(0.0001F, rightStop->position - leftStop->position);
    const float factor = std::clamp((t - leftStop->position) / span, 0.0F, 1.0F);
    return MaterialEditorPanelColorRampStopModel{
        .position = t,
        .r = leftStop->r + ((rightStop->r - leftStop->r) * factor),
        .g = leftStop->g + ((rightStop->g - leftStop->g) * factor),
        .b = leftStop->b + ((rightStop->b - leftStop->b) * factor),
    };
}

void DrawColorRampWatcher(HDC dc, const RECT& nodeRect, const kb::render::RenderMaterialGraphNode& node) {
    if (node.kind != kb::render::RenderMaterialGraphNodeKind::ColorRamp) {
        return;
    }
    const float scale = NodeUiScale(nodeRect, node.kind);
    const RECT watcher = MaterialEditorPanelColorWatcherRect(nodeRect, node.kind);
    FillRoundedRect(dc, watcher, RGB(25, 28, 33), std::max(5, ScaleMetric(7, scale)));
    StrokeRoundedRect(dc, watcher, RGB(58, 68, 82), std::max(5, ScaleMetric(7, scale)));

    std::vector<MaterialEditorPanelColorRampStopModel> stops = MaterialEditorPanelColorRampStops(node.parameter.defaultValueHint);
    std::ranges::sort(stops, {}, &MaterialEditorPanelColorRampStopModel::position);
    const RECT firstSwatch = MaterialEditorPanelColorWatcherSwatchRect(nodeRect, node.kind);
    DrawColorSwatch(dc, firstSwatch, MaterialEditorPanelColorValue(stops.front().r, stops.front().g, stops.front().b), scale);
    const RECT gradient = MaterialEditorPanelColorRampGradientRect(nodeRect);
    DrawColorCheckerboard(dc, gradient, std::max(3, ScaleMetric(5, scale)));
    const int width = std::max(1L, gradient.right - gradient.left);
    for (int column = 0; column < width; ++column) {
        const float t = static_cast<float>(column) / static_cast<float>(std::max(1, width - 1));
        const MaterialEditorPanelColorRampStopModel sample = ColorRampSample(stops, t);
        GdiDrawing::FillRectColor(
            dc,
            RECT{ gradient.left + column, gradient.top, gradient.left + column + 1, gradient.bottom },
            RGB(ColorByte(sample.r), ColorByte(sample.g), ColorByte(sample.b)));
    }
    StrokeRoundedRect(dc, gradient, RGB(14, 17, 21), std::max(4, ScaleMetric(5, scale)));
    DrawGraphText(
        dc,
        RECT{ gradient.left, gradient.bottom + ScaleMetric(22, scale), gradient.right, watcher.bottom },
        "Gradient stops",
        BlenderGraphTheme::TextMuted,
        ScaleMetric(kGraphPinFontSize, scale),
        FW_NORMAL,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    for (std::size_t stopIndex = 0U; stopIndex < 2U && stopIndex < stops.size(); ++stopIndex) {
        const int centerX = gradient.left + static_cast<int>(
            std::round(static_cast<float>(std::max(1L, gradient.right - gradient.left)) * stops[stopIndex].position));
        const RECT stopRect{
            centerX - ScaleMetric(7, scale),
            gradient.bottom + ScaleMetric(5, scale),
            centerX + ScaleMetric(7, scale),
            gradient.bottom + ScaleMetric(19, scale),
        };
        DrawColorSwatch(dc, stopRect, MaterialEditorPanelColorValue(stops[stopIndex].r, stops[stopIndex].g, stops[stopIndex].b), scale);
    }
}

[[nodiscard]] std::vector<MaterialEditorGraphDiagnosticMarker> MarkersForNode(
    const EditorSceneContext& sceneContext,
    std::uint32_t nodeId) {
    std::vector<MaterialEditorGraphDiagnosticMarker> markers;
    for (const MaterialEditorGraphDiagnosticMarker& marker : sceneContext.MaterialEditor().GraphDiagnosticMarkers()) {
        if (marker.nodeId == nodeId) {
            markers.push_back(marker);
        }
    }
    return markers;
}

[[nodiscard]] COLORREF MarkerColor(kb::render::RenderMaterialGraphDiagnosticSeverity severity) noexcept {
    switch (severity) {
    case kb::render::RenderMaterialGraphDiagnosticSeverity::Error:
        return RGB(229, 86, 91);
    case kb::render::RenderMaterialGraphDiagnosticSeverity::Warning:
        return RGB(226, 170, 77);
    }
    return RGB(226, 170, 77);
}

void DrawGraphDiagnosticMarker(
    HDC dc,
    const RECT& nodeRect,
    const std::vector<MaterialEditorGraphDiagnosticMarker>& markers,
    float scale) {
    if (markers.empty()) {
        return;
    }
    const bool hasError = std::ranges::any_of(markers, [](const MaterialEditorGraphDiagnosticMarker& marker) {
        return marker.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error;
    });
    const MaterialEditorGraphDiagnosticMarker& primary = *std::ranges::find_if(markers, [hasError](const MaterialEditorGraphDiagnosticMarker& marker) {
        return !hasError || marker.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error;
    });
    const int size = ScaleMetric(22, scale);
    const RECT badge{
        nodeRect.right - ScaleMetric(10, scale) - size,
        nodeRect.top + ScaleMetric(5, scale),
        nodeRect.right - ScaleMetric(10, scale),
        nodeRect.top + ScaleMetric(5, scale) + size,
    };
    const COLORREF fill = MarkerColor(primary.severity);
    FillRoundedRect(dc, badge, fill, std::max(4, ScaleMetric(6, scale)));
    StrokeRoundedRect(dc, badge, RGB(24, 19, 20), std::max(4, ScaleMetric(6, scale)), 1);
    DrawGraphText(dc, badge, hasError ? "!" : "?", RGB(255, 250, 244), ScaleMetric(13, scale), FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (markers.size() > 1U) {
        const std::string count = std::to_string(markers.size());
        const RECT countRect{ badge.left - ScaleMetric(18, scale), badge.top, badge.left - ScaleMetric(3, scale), badge.bottom };
        DrawGraphText(dc, countRect, count.c_str(), RGB(255, 225, 212), ScaleMetric(9, scale), FW_SEMIBOLD, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawGraphNode(
    HDC dc,
    const RECT& rect,
    const RECT& clip,
    const kb::render::RenderMaterialGraphDocument& graph,
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialGraphNode& node,
    bool selected,
    const kb::render::RenderMaterialAssetData* material,
    const EditorSceneContext& sceneContext) {
    const float scale = NodeUiScale(rect, node.kind);
    const int headerHeight = ScaleMetric(kGraphNodeHeaderHeight, scale);
    const int cornerDiameter = ScaleMetric(kGraphNodeCornerDiameter, scale);
    const int pinRadius = ScaleMetric(kGraphNodePinRadius, scale);
    const COLORREF body = BlenderGraphTheme::NodeBody;
    const COLORREF bodyTop = BlenderGraphTheme::NodeBody;
    const COLORREF bodyBottom = BlenderGraphTheme::NodeBodyBottom;
    const COLORREF headerTop = ScaleColor(GraphNodeHeaderColor(node.kind), selected ? 1.12F : 1.0F);
    const COLORREF headerBottom = ScaleColor(headerTop, 0.86F);
    const COLORREF border = selected ? BlenderGraphTheme::NodeOutlineSelected : BlenderGraphTheme::NodeOutline;
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics nodeGraphics(dc);
    nodeGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    nodeGraphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    nodeGraphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    nodeGraphics.SetClip(Gdiplus::Rect(
        static_cast<int>(clip.left),
        static_cast<int>(clip.top),
        std::max(0, static_cast<int>(clip.right - clip.left)),
        std::max(0, static_cast<int>(clip.bottom - clip.top))));

    DrawGraphNodeFrame(dc, nodeGraphics, rect, body, scale);
    const RECT inner{ rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2 };
    DrawVerticalGradientClippedToRound(dc, RECT{ inner.left, rect.top + headerHeight, inner.right, inner.bottom }, inner, bodyTop, bodyBottom, std::max(2, cornerDiameter - 2));
    DrawVerticalGradientClippedToRound(dc, RECT{ inner.left, inner.top, inner.right, rect.top + headerHeight }, inner, headerTop, headerBottom, std::max(2, cornerDiameter - 2));
    GdiDrawing::FillRectColor(dc, RECT{ rect.left + 2, rect.top + headerHeight, rect.right - 2, rect.top + headerHeight + 1 }, RGB(58, 96, 148));
    StrokeRoundedRect(dc, rect, border, cornerDiameter, selected ? 2 : 1);

    std::string title = node.parameter.displayName.empty() ? GraphNodeTitle(node.kind) : node.parameter.displayName;
    const std::string_view supportTag = kb::render::RenderMaterialGraphNodeSupportShortTag(node.kind);
    if (!supportTag.empty()) {
        title += "  [";
        title += supportTag;
        title += "]";
    }
    DrawGraphText(dc, RECT{ rect.left + ScaleMetric(12, scale), rect.top, rect.right - ScaleMetric(12, scale), rect.top + headerHeight }, title.c_str(), RGB(242, 242, 242), ScaleMetric(kGraphTitleFontSize, scale), FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    const std::vector<MaterialEditorGraphDiagnosticMarker> diagnosticMarkers = MarkersForNode(sceneContext, node.id);
    DrawGraphDiagnosticMarker(dc, rect, diagnosticMarkers, scale);

    DrawTextureSamplePreview(dc, rect, node, material, sceneContext);
    DrawTextureParameterValue(dc, rect, node, material, sceneContext);
    DrawConstantValue(dc, rect, node, sceneContext);
    DrawParameterColorValue(dc, rect, node, material);
    DrawColorRampWatcher(dc, rect, node);

    const std::vector<std::pair<std::string, std::string>>inputPins = GraphInputPins(node);
    if (!inputPins.empty()) {
        for (std::size_t index = 0; index < inputPins.size(); ++index) {
            const POINT scaledPin = InputPinPoint(rect, node, inputPins[index].first);
            DrawGraphPin(
                nodeGraphics,
                scaledPin,
                GraphInputPinColor(node, inputPins[index].first),
                scale,
                false,
                GraphPinDragStateForNode(graph, sceneContext, assetId, node, inputPins[index].first, false));
            const bool textureSample = MaterialEditorPanelIsTextureSamplePreviewNode(node.kind);
            const RECT texturePreview = textureSample ? MaterialEditorPanelTextureSamplePreviewRect(rect) : RECT{};
            const int inputLabelRight = textureSample
                ? std::max(rect.left + ScaleMetric(54, scale), texturePreview.left - ScaleMetric(10, scale))
                : rect.right - ScaleMetric(18, scale);
            DrawGraphText(
                dc,
                RECT{
                    rect.left + pinRadius + ScaleMetric(textureSample ? 8 : 16, scale),
                    scaledPin.y - ScaleMetric(11, scale),
                    inputLabelRight,
                    scaledPin.y + ScaleMetric(11, scale),
                },
                std::string{ inputPins[index].second }.c_str(),
                BlenderGraphTheme::Text,
                ScaleMetric(kGraphPinFontSize, scale),
                FW_NORMAL,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
    const std::vector<std::pair<std::string, std::string>>outputPins = GraphOutputPins(node);
    for (std::size_t index = 0U; index < outputPins.size(); ++index) {
        const POINT output = OutputPinPoint(rect, node.kind, index, outputPins.size());
        DrawGraphPin(
            nodeGraphics,
            output,
            GraphOutputPinColor(node, outputPins[index].first),
            scale,
            GraphOutputPinTinted(node.kind, outputPins[index].first),
            GraphPinDragStateForNode(graph, sceneContext, assetId, node, outputPins[index].first, true));
        RECT outputLabelRect{
            rect.left + ScaleMetric(8, scale),
            output.y - ScaleMetric(11, scale),
            output.x - pinRadius - ScaleMetric(8, scale),
            output.y + ScaleMetric(11, scale),
        };
        if (MaterialEditorPanelIsTextureSamplePreviewNode(node.kind)) {
            const RECT texturePreview = MaterialEditorPanelTextureSamplePreviewRect(rect);
            outputLabelRect.left = texturePreview.right + ScaleMetric(8, scale);
            outputLabelRect.right = output.x - pinRadius - ScaleMetric(8, scale);
        } else if (MaterialEditorPanelIsTextureObjectPreviewNode(node.kind)) {
            const RECT textureValue = MaterialEditorPanelTextureParameterRect(rect);
            outputLabelRect.left = textureValue.right + ScaleMetric(8, scale);
            outputLabelRect.right = output.x - pinRadius - ScaleMetric(8, scale);
        }
        if (MaterialEditorPanelIsConstantNode(node.kind)) {
            RECT valueRect = MaterialEditorPanelConstantValueRect(rect);
            if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector2) {
                valueRect = MaterialEditorPanelConstantVectorFieldsBounds(rect, 2U);
            } else if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector) {
                valueRect = MaterialEditorPanelColorWatcherRect(rect, node.kind);
            } else if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor) {
                valueRect = MaterialEditorPanelColorWatcherRect(rect, node.kind);
            }
            outputLabelRect.left = std::max(outputLabelRect.left, valueRect.right + ScaleMetric(8, scale));
        } else if (MaterialEditorPanelNodeHasColorWatcher(node.kind)) {
            const RECT watcher = MaterialEditorPanelColorWatcherRect(rect, node.kind);
            outputLabelRect.left = std::max(outputLabelRect.left, watcher.right + ScaleMetric(8, scale));
        }
        if (outputLabelRect.right > outputLabelRect.left + ScaleMetric(10, scale)) {
            DrawGraphText(
                dc,
                outputLabelRect,
                std::string{ outputPins[index].second }.c_str(),
                GraphOutputPinLabelColor(node.kind, outputPins[index].first),
                ScaleMetric(kGraphPinFontSize, scale),
                FW_NORMAL,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
}

void DrawPendingGraphConnection(HDC dc, const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, const EditorSceneContext& sceneContext, kb::assets::AssetId assetId) {
    if (!sceneContext.HasMaterialGraphPinConnection() || sceneContext.MaterialGraphPinConnectionAssetId() != assetId) {
        return;
    }
    const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(graph, sceneContext.MaterialGraphPinConnectionNodeId());
    if (node == nullptr) {
        return;
    }
    const std::optional<RECT> nodeRect = MaterialEditorPanelRenderer::GraphNodeRect(content, graph, node->id, sceneContext, assetId);
    if (!nodeRect.has_value()) {
        return;
    }

    POINT anchor{};
    if (sceneContext.MaterialGraphPinConnectionIsOutput()) {
        const std::vector<std::pair<std::string, std::string>>outputPins = GraphOutputPins(*node);
        std::size_t pinIndex = 0U;
        for (std::size_t index = 0U; index < outputPins.size(); ++index) {
            if (outputPins[index].first == sceneContext.MaterialGraphPinConnectionPin()) {
                pinIndex = index;
                break;
            }
        }
        anchor = OutputPinPoint(*nodeRect, node->kind, pinIndex, outputPins.size());
    } else {
        const std::vector<std::pair<std::string, std::string>>inputPins = GraphInputPins(*node);
        std::string_view pinName = inputPins.empty() ? std::string_view{} : inputPins.front().first;
        for (const auto& inputPin : inputPins) {
            if (inputPin.first == sceneContext.MaterialGraphPinConnectionPin()) {
                pinName = inputPin.first;
                break;
            }
        }
        anchor = InputPinPoint(*nodeRect, *node, pinName);
    }

    const POINT cursor{ sceneContext.MaterialGraphPinConnectionX(), sceneContext.MaterialGraphPinConnectionY() };
    COLORREF pendingColor = sceneContext.MaterialGraphPinConnectionIsOutput()
        ? GraphOutputPinColor(*node, sceneContext.MaterialGraphPinConnectionPin())
        : GraphInputPinColor(*node, sceneContext.MaterialGraphPinConnectionPin());
    if (const std::optional<MaterialEditorGraphPinHit> hoverPin =
            MaterialEditorPanelRenderer::GraphPinAt(content, graph, sceneContext, assetId, cursor.x, cursor.y)) {
        const MaterialEditorGraphPinDragState hoverState = MaterialEditorPanelRenderer::GraphPinDragState(
            graph,
            sceneContext.MaterialGraphPinConnectionNodeId(),
            sceneContext.MaterialGraphPinConnectionPin(),
            sceneContext.MaterialGraphPinConnectionIsOutput(),
            hoverPin->nodeId,
            hoverPin->pin,
            hoverPin->direction == MaterialEditorGraphPinDirection::Output);
        if (hoverState == MaterialEditorGraphPinDragState::Compatible) {
            pendingColor = RGB(92, 210, 126);
        } else if (hoverState == MaterialEditorGraphPinDragState::Incompatible) {
            pendingColor = RGB(231, 88, 88);
        }
    }
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics pendingLinkGraphics(dc);
    pendingLinkGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    pendingLinkGraphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    DrawGraphBezier(
        pendingLinkGraphics,
        sceneContext.MaterialGraphPinConnectionIsOutput() ? anchor : cursor,
        sceneContext.MaterialGraphPinConnectionIsOutput() ? cursor : anchor,
        pendingColor,
        std::clamp(ScaleMetric(3, sceneContext.MaterialGraphZoom()), 1, 3));
}

void DrawGraphBoxSelection(HDC dc, const EditorSceneContext& sceneContext) {
    if (!sceneContext.IsMaterialGraphBoxSelecting()) {
        return;
    }
    const RECT selection = MaterialEditorPanelNormalizedRect(RECT{
        sceneContext.MaterialGraphBoxSelectionStartX(),
        sceneContext.MaterialGraphBoxSelectionStartY(),
        sceneContext.MaterialGraphBoxSelectionCurrentX(),
        sceneContext.MaterialGraphBoxSelectionCurrentY(),
    });
    if (MaterialEditorPanelRectWidth(selection) < 2 && MaterialEditorPanelRectHeight(selection) < 2) {
        return;
    }
    StrokeRoundedRect(dc, selection, sceneContext.MaterialGraphBoxSelectionAdditive() ? RGB(118, 174, 255) : RGB(92, 145, 224), 4, 1);
}

void DrawGraphContextMenu(HDC dc, const EditorSceneContext& sceneContext) {
    if (!sceneContext.IsMaterialGraphContextMenuOpen()) {
        return;
    }

    const RECT menu = MaterialEditorPanelRenderer::GraphContextMenuRect(sceneContext);
    GdiDrawing::DrawSharpFrame(dc, menu, RGB(28, 31, 36), RGB(47, 52, 61));
    const RECT search{
        menu.left + kMaterialEditorGraphMenuPadding + 8,
        menu.top + kMaterialEditorGraphMenuPadding,
        menu.right - kMaterialEditorGraphMenuPadding - 8,
        menu.top + kMaterialEditorGraphMenuPadding + 22,
    };
    StrokeRoundedRect(dc, search, RGB(55, 111, 197), 12, 1);
    const std::string searchText = sceneContext.MaterialGraphContextMenuSearchQuery().empty()
        ? std::string{ "Search nodes..." }
        : std::string{ sceneContext.MaterialGraphContextMenuSearchQuery() };
    DrawText(
        dc,
        RECT{ search.left + 10, search.top, search.right - 10, search.bottom },
        searchText.c_str(),
        sceneContext.MaterialGraphContextMenuSearchQuery().empty() ? RGB(172, 184, 198) : RGB(236, 242, 249),
        10);

    const RECT viewport = MaterialEditorGraphContextMenuViewportRect(menu);
    const int maxScroll = MaterialEditorGraphContextMenuMaxScroll(sceneContext);
    const int scrollOffset = std::clamp(sceneContext.MaterialGraphContextMenuScrollOffset(), 0, maxScroll);
    int y = viewport.top - scrollOffset;
    const std::size_t selectedGraphNodeCount = sceneContext.SelectedMaterialGraphNodeIds().size();
    const bool hasSelectedGraphComment = sceneContext.SelectedMaterialGraphCommentId() != 0U;
    const std::vector<MaterialEditorGraphMenuCommand>& favoriteCommands = sceneContext.MaterialGraphPaletteFavoriteCommands();
    const auto drawCommandRow = [&](MaterialEditorGraphMenuCommand command, std::size_t categoryIndex) {
        const bool enabled = MaterialEditorGraphContextMenuCommandEnabled(command, selectedGraphNodeCount, hasSelectedGraphComment);
        const bool commandHovered = sceneContext.IsMaterialGraphContextMenuCommandHovered(categoryIndex, command);
        const bool favorite = sceneContext.IsMaterialGraphPaletteFavorite(command);
        const RECT commandFill{ menu.left + 8, y, menu.right - 8, y + kMaterialEditorGraphMenuCommandHeight };
        GdiDrawing::FillRectColor(
            dc,
            commandFill,
            commandHovered
                ? ProjectFilesPanelDrawing::Blend(RGB(24, 27, 33), RGB(166, 178, 193), 14)
                : RGB(24, 27, 33));
        const RECT favoriteRect{ menu.left + 11, y + 5, menu.left + 23, y + 17 };
        GdiDrawing::DrawSharpFrame(dc, favoriteRect, favorite ? RGB(92, 145, 224) : RGB(66, 74, 86), RGB(32, 36, 43));
        if (favorite) {
            GdiDrawing::FillRectColor(dc, RECT{ favoriteRect.left + 3, favoriteRect.top + 3, favoriteRect.right - 3, favoriteRect.bottom - 3 }, RGB(118, 174, 255));
        }
        const RECT commandRow{ menu.left + 32, y, menu.right - 12, y + kMaterialEditorGraphMenuCommandHeight };
        DrawText(
            dc,
            commandRow,
            std::string{ MaterialEditorGraphContextMenuCommandName(command) }.c_str(),
            enabled ? (commandHovered ? RGB(248, 250, 252) : RGB(213, 222, 235)) : RGB(102, 112, 126),
            10,
            commandHovered ? FW_SEMIBOLD : FW_NORMAL);
        y += kMaterialEditorGraphMenuCommandHeight;
    };

    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
    if (MaterialEditorGraphContextMenuUsesFlatCommandList(sceneContext)) {
        const std::vector<MaterialEditorGraphMenuCommand> commands = MaterialEditorGraphContextMenuFilteredCommands(sceneContext);
        for (const MaterialEditorGraphMenuCommand command : commands) {
            drawCommandRow(command, 0U);
        }
        RestoreDC(dc, savedDc);
        if (maxScroll > 0) {
            const int trackLeft = menu.right - 7;
            GdiDrawing::FillRectColor(dc, RECT{ trackLeft, viewport.top, trackLeft + 3, viewport.bottom }, RGB(36, 41, 49));
            const int viewportHeight = std::max(1L, viewport.bottom - viewport.top);
            const int contentHeight = std::max(1, MaterialEditorGraphContextMenuScrollableContentHeight(sceneContext));
            const int thumbHeight = std::max(28, viewportHeight * viewportHeight / contentHeight);
            const int thumbTop = viewport.top + (viewportHeight - thumbHeight) * scrollOffset / std::max(1, maxScroll);
            GdiDrawing::FillRectColor(dc, RECT{ trackLeft - 1, thumbTop, trackLeft + 4, thumbTop + thumbHeight }, RGB(96, 111, 130));
        }
        return;
    }

    for (std::size_t categoryIndex = 0U; categoryIndex < MaterialEditorGraphContextMenuCategoryCount(); ++categoryIndex) {
        const bool expanded = sceneContext.IsMaterialGraphContextMenuCategoryExpanded(categoryIndex);
        const bool categoryHovered = sceneContext.IsMaterialGraphContextMenuCategoryHovered(categoryIndex);
        const RECT categoryFill{ menu.left + 8, y, menu.right - 8, y + kMaterialEditorGraphMenuCategoryHeight };
        GdiDrawing::FillRectColor(
            dc,
            categoryFill,
            categoryHovered
                ? ProjectFilesPanelDrawing::Blend(RGB(31, 35, 42), RGB(166, 178, 193), 12)
                : RGB(31, 35, 42));
        GdiDrawing::FillRectColor(dc, RECT{ categoryFill.left, categoryFill.bottom - 1, categoryFill.right, categoryFill.bottom }, RGB(20, 23, 28));
        const RECT disclosure{ categoryFill.left + 4, categoryFill.top + 3, categoryFill.left + 18, categoryFill.bottom - 3 };
        ProjectFilesPanelDrawing::DrawDisclosureTriangle(dc, disclosure, categoryHovered ? RGB(224, 235, 247) : RGB(180, 191, 206), expanded);
        const RECT categoryText{ categoryFill.left + 22, categoryFill.top, categoryFill.right - 8, categoryFill.bottom };
        const std::string label{ MaterialEditorGraphContextMenuCategoryName(categoryIndex) };
        DrawText(dc, categoryText, label.c_str(), categoryHovered ? RGB(246, 249, 252) : RGB(231, 237, 245), 11, FW_SEMIBOLD);
        y += kMaterialEditorGraphMenuCategoryHeight;
        if (!expanded) {
            continue;
        }

        const std::vector<MaterialEditorGraphMenuCommand> commands = MaterialEditorGraphContextMenuCommands(categoryIndex, favoriteCommands);
        for (const MaterialEditorGraphMenuCommand command : commands) {
            drawCommandRow(command, categoryIndex);
        }
    }
    RestoreDC(dc, savedDc);
    if (maxScroll > 0) {
        const int trackLeft = menu.right - 7;
        GdiDrawing::FillRectColor(dc, RECT{ trackLeft, viewport.top, trackLeft + 3, viewport.bottom }, RGB(36, 41, 49));
        const int viewportHeight = std::max(1L, viewport.bottom - viewport.top);
        const int contentHeight = std::max(1, MaterialEditorGraphContextMenuScrollableContentHeight(sceneContext));
        const int thumbHeight = std::max(28, viewportHeight * viewportHeight / contentHeight);
        const int thumbTop = viewport.top + (viewportHeight - thumbHeight) * scrollOffset / std::max(1, maxScroll);
        GdiDrawing::FillRectColor(dc, RECT{ trackLeft - 1, thumbTop, trackLeft + 4, thumbTop + thumbHeight }, RGB(96, 111, 130));
    }
}

void DrawGraphCanvas(HDC dc, const RECT& content, const kb::render::RenderMaterialAssetData& material, const EditorSceneContext& sceneContext, kb::assets::AssetId assetId, std::uint32_t selectedNodeId) {
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    DrawGraphGrid(dc, layout.graphCanvas, sceneContext.MaterialGraphZoom(), sceneContext.MaterialGraphPanX(), sceneContext.MaterialGraphPanY());

    const kb::render::RenderMaterialGraphDocument& graph = material.graph;
    const kb::render::RenderMaterialGraphDocument defaultGraph = graph.nodes.empty()
        ? kb::render::MakeDefaultRenderMaterialGraphDocument()
        : kb::render::RenderMaterialGraphDocument{};
    const kb::render::RenderMaterialGraphDocument& graphView = graph.nodes.empty() ? defaultGraph : graph;

    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, layout.graphCanvas.left, layout.graphCanvas.top, layout.graphCanvas.right, layout.graphCanvas.bottom);
    for (const kb::render::RenderMaterialGraphCompositeSubgraph& composite : graphView.composites) {
        const std::optional<RECT> compositeRect = MaterialEditorPanelRenderer::GraphCompositeRect(content, graphView, composite.id, sceneContext);
        if (compositeRect.has_value()) {
            DrawGraphCompositeBox(dc, *compositeRect, composite);
        }
    }
    for (const kb::render::RenderMaterialGraphCommentBox& comment : graphView.comments) {
        const std::optional<RECT> commentRect = MaterialEditorPanelRenderer::GraphCommentRect(content, graphView, comment.id, sceneContext);
        if (commentRect.has_value()) {
            DrawGraphCommentBox(dc, *commentRect, comment, sceneContext.IsMaterialGraphCommentSelected(comment.id));
        }
    }
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics linkGraphics(dc);
    linkGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    linkGraphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    for (const kb::render::RenderMaterialGraphLink& link : graphView.links) {
        if (GraphNodeHiddenByCollapsedComposite(graphView, link.fromNodeId) ||
            GraphNodeHiddenByCollapsedComposite(graphView, link.toNodeId)) {
            continue;
        }
        const std::optional<RECT> from = MaterialEditorPanelRenderer::GraphNodeRect(content, graphView, link.fromNodeId, sceneContext, assetId);
        const std::optional<RECT> to = MaterialEditorPanelRenderer::GraphNodeRect(content, graphView, link.toNodeId, sceneContext, assetId);
        if (from.has_value() && to.has_value()) {
            const kb::render::RenderMaterialGraphNode* fromNode = kb::render::FindRenderMaterialGraphNode(graphView, link.fromNodeId);
            const kb::render::RenderMaterialGraphNode* toNode = kb::render::FindRenderMaterialGraphNode(graphView, link.toNodeId);
            if (fromNode != nullptr && toNode != nullptr) {
                DrawGraphLink(linkGraphics, *from, *to, link.fromPin, link.toPin, *fromNode, *toNode);
            }
        }
    }
    for (const kb::render::RenderMaterialGraphNode& node : graphView.nodes) {
        if (GraphNodeHiddenByCollapsedComposite(graphView, node.id)) {
            continue;
        }
        const std::optional<RECT> nodeRect = MaterialEditorPanelRenderer::GraphNodeRect(content, graphView, node.id, sceneContext, assetId);
        if (nodeRect.has_value()) {
            DrawGraphNode(
                dc,
                *nodeRect,
                layout.graphCanvas,
                graph,
                assetId,
                node,
                sceneContext.IsMaterialGraphNodeSelected(node.id) || node.id == selectedNodeId,
                graph.nodes.empty() ? nullptr : &material,
                sceneContext);
        }
    }
    DrawGraphBoxSelection(dc, sceneContext);
    DrawPendingGraphConnection(dc, content, graphView, sceneContext, assetId);
    DrawGraphContextMenu(dc, sceneContext);
    RestoreDC(dc, savedDc);
}

struct MaterialEditorDocumentView {
    std::optional<kb::render::RenderMaterialAssetData> material{};
    std::string assetKind;
    kb::assets::AssetId parentMaterialAssetId{};
    std::vector<std::string> diagnostics;
    bool hasErrorDiagnostic = false;
};

[[nodiscard]] bool IsMaterialDocument(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance";
}

[[nodiscard]] std::filesystem::path ResolveAssetPath(const kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata) {
    if (!metadata.physicalPath.empty()) {
        return metadata.physicalPath;
    }
    return manager.Mounts().Resolve(metadata.virtualPath).value_or(std::filesystem::path{});
}

void AppendMaterialDiagnostics(std::vector<std::string>& lines, bool& hasError, const kb::render::RenderMaterialAssetParseResult& result) {
    const MaterialEditorPanelDiagnosticRows rows = MaterialEditorPanelRenderer::DiagnosticRows(result);
    hasError = hasError || rows.hasError;
    lines.insert(lines.end(), rows.rows.begin(), rows.rows.end());
}

void AppendMaterialGraphDiagnostics(std::vector<std::string>& lines, bool& hasError, const kb::render::RenderMaterialAssetData& material) {
    const std::vector<kb::render::RenderMaterialGraphDiagnostic> diagnostics = kb::render::ValidateRenderMaterialAssetGraphDiagnostics(material);
    for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : diagnostics) {
        std::ostringstream line;
        line << kb::render::RenderMaterialGraphDiagnosticSeverityName(diagnostic.severity)
             << " graph." << kb::render::RenderMaterialGraphDiagnosticKindName(diagnostic.kind);
        if (diagnostic.nodeId != 0U) {
            line << " node " << diagnostic.nodeId;
        }
        if (diagnostic.linkId != 0U) {
            line << " link " << diagnostic.linkId;
        }
        if (!diagnostic.pin.empty()) {
            line << " pin " << diagnostic.pin;
        }
        line << ": " << diagnostic.message;
        lines.push_back(line.str());
        hasError = hasError || diagnostic.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error;
    }
}

void AppendMaterialInstanceDiagnostics(std::vector<std::string>& lines, bool& hasError, const kb::render::RenderMaterialInstanceAssetParseResult& result) {
    for (const kb::render::RenderMaterialInstanceAssetParseDiagnostic& diagnostic : result.diagnostics) {
        hasError = true;
        std::ostringstream line;
        line << "Error " << kb::render::RenderMaterialInstanceAssetParseDiagnosticCodeName(diagnostic.code);
        if (diagnostic.line > 0U) {
            line << " line " << diagnostic.line;
        }
        if (!diagnostic.field.empty()) {
            line << " " << diagnostic.field;
        }
        line << ": " << diagnostic.message;
        if (!diagnostic.text.empty()) {
            line << " [" << diagnostic.text << "]";
        }
        lines.push_back(line.str());
    }
}

[[nodiscard]] std::optional<MaterialEditorDocumentView> ReadDocumentView(
    const EditorSceneContext& sceneContext,
    const kb::assets::AssetMetadata& metadata) {
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const std::filesystem::path path = ResolveAssetPath(manager, metadata);
    if (path.empty()) {
        return MaterialEditorDocumentView{
            .material = std::nullopt,
            .assetKind = metadata.type == "RenderMaterialInstance" ? "Material Instance" : "Material",
            .parentMaterialAssetId = {},
            .diagnostics = { "Error file_open_failed: Material document path could not be resolved." },
            .hasErrorDiagnostic = true,
        };
    }

    if (metadata.type == "RenderMaterial") {
        if (sceneContext.MaterialEditor().OpenAssetId() == metadata.id && sceneContext.MaterialEditor().WorkingCopy().has_value()) {
            std::vector<std::string> diagnostics = sceneContext.MaterialEditor().Diagnostics();
            return MaterialEditorDocumentView{
                .material = sceneContext.MaterialEditor().WorkingCopy(),
                .assetKind = "Material",
                .parentMaterialAssetId = {},
                .diagnostics = std::move(diagnostics),
                .hasErrorDiagnostic = sceneContext.MaterialEditor().DiagnosticsHaveError(),
            };
        }
        kb::render::RenderMaterialAssetParseResult result = kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(path, metadata.id);
        std::vector<std::string> diagnostics;
        bool hasError = false;
        AppendMaterialDiagnostics(diagnostics, hasError, result);
        if (result.asset.has_value()) {
            AppendMaterialGraphDiagnostics(diagnostics, hasError, *result.asset);
        }
        return MaterialEditorDocumentView{
            .material = std::move(result.asset),
            .assetKind = "Material",
            .parentMaterialAssetId = {},
            .diagnostics = std::move(diagnostics),
            .hasErrorDiagnostic = hasError,
        };
    }

    if (metadata.type != "RenderMaterialInstance") {
        return std::nullopt;
    }

    kb::render::RenderMaterialInstanceAssetParseResult instance = kb::render::RenderMaterialInstanceAssetLoader::LoadInstanceWithDiagnostics(path, metadata.id);
    std::vector<std::string> diagnostics;
    bool hasError = false;
    AppendMaterialInstanceDiagnostics(diagnostics, hasError, instance);
    if (!instance.asset.has_value() || !instance.asset->parentMaterialAssetId.IsValid()) {
        return MaterialEditorDocumentView{
            .material = std::nullopt,
            .assetKind = "Material Instance",
            .parentMaterialAssetId = {},
            .diagnostics = std::move(diagnostics),
            .hasErrorDiagnostic = true,
        };
    }

    const kb::assets::AssetMetadata* parentMetadata = manager.Registry().Find(instance.asset->parentMaterialAssetId);
    std::optional<kb::render::RenderMaterialAssetData> parent =
        parentMetadata != nullptr ? sceneContext.ReadMaterialDocumentAsset(parentMetadata->id) : std::nullopt;
    if (parentMetadata == nullptr || !parent.has_value()) {
        diagnostics.push_back("Error missing_parent_material: Parent material asset could not be resolved.");
        hasError = true;
    }
    std::optional<kb::render::RenderMaterialAssetData> effectiveMaterial;
    if (parent.has_value()) {
        effectiveMaterial = kb::render::BuildEffectiveRenderMaterialInstanceAsset(*parent, *instance.asset);
    }
    return MaterialEditorDocumentView{
        .material = std::move(effectiveMaterial),
        .assetKind = "Material Instance",
        .parentMaterialAssetId = instance.asset->parentMaterialAssetId,
        .diagnostics = std::move(diagnostics),
        .hasErrorDiagnostic = hasError,
    };
}

void DrawGraphOverlay(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
}

[[nodiscard]] COLORREF CookBannerColor(EditorMaterialGraphCookBannerSeverity severity) noexcept {
    switch (severity) {
    case EditorMaterialGraphCookBannerSeverity::Ready:
        return RGB(126, 201, 143);
    case EditorMaterialGraphCookBannerSeverity::Pending:
        return RGB(214, 196, 120);
    case EditorMaterialGraphCookBannerSeverity::Warning:
        return RGB(226, 170, 104);
    case EditorMaterialGraphCookBannerSeverity::Error:
        return RGB(232, 112, 112);
    case EditorMaterialGraphCookBannerSeverity::None:
        break;
    }
    return RGB(86, 92, 100);
}

void DrawPreviewOverlay(
    HDC dc,
    const MaterialEditorPanelLayout& layout,
    const EditorMaterialPreviewTelemetry& telemetry,
    const EditorMaterialGraphCookBanner& cookBanner) {
    DrawGraphOverlay(dc, layout.previewFrame, RGB(9, 10, 12), RGB(54, 58, 66));
    DrawText(dc, layout.previewFrame, telemetry.materialLoaded ? "Preview" : "Preview unavailable", RGB(86, 92, 100), 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // MAT-32: surface the live graph cook/program state over the preview so the artist sees whether
    // the GPU graph program is ready, compiling, fell back, or failed - never a silent black frame.
    if (cookBanner.severity != EditorMaterialGraphCookBannerSeverity::None && !cookBanner.label.empty()) {
        const RECT bannerRect{
            layout.previewFrame.left + 6,
            layout.previewFrame.top + 6,
            layout.previewFrame.right - 6,
            layout.previewFrame.top + 26,
        };
        DrawText(dc, bannerRect, cookBanner.label.c_str(), CookBannerColor(cookBanner.severity), 10, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void DrawDiagnosticsPanel(HDC dc, const MaterialEditorPanelLayout& layout, const MaterialEditorDocumentView& document) {
    if (document.diagnostics.empty() || RectWidth(layout.diagnosticsPanel) < 160 || RectHeight(layout.diagnosticsPanel) < 72) {
        return;
    }

    const COLORREF border = document.hasErrorDiagnostic ? RGB(151, 76, 76) : RGB(137, 109, 54);
    const COLORREF titleColor = document.hasErrorDiagnostic ? RGB(247, 171, 171) : RGB(239, 203, 127);
    DrawGraphOverlay(dc, layout.diagnosticsPanel, RGB(29, 26, 24), border);
    DrawText(dc, RECT{ layout.diagnosticsPanel.left + 10, layout.diagnosticsPanel.top + 6, layout.diagnosticsPanel.right - 10, layout.diagnosticsPanel.top + 28 }, "Diagnostics", titleColor, 12, FW_SEMIBOLD);
    const int rowTop = layout.diagnosticsPanel.top + 32;
    const int rowHeight = 22;
    const int availableRows = (static_cast<int>(layout.diagnosticsPanel.bottom) - rowTop - 8) / rowHeight;
    const std::size_t visibleRows = static_cast<std::size_t>(std::max(0, availableRows));
    const std::size_t count = std::min(visibleRows, document.diagnostics.size());
    for (std::size_t index = 0; index < count; ++index) {
        const RECT row{
            layout.diagnosticsPanel.left + 10,
            rowTop + static_cast<int>(index) * rowHeight,
            layout.diagnosticsPanel.right - 10,
            rowTop + static_cast<int>(index + 1U) * rowHeight,
        };
        DrawText(dc, row, document.diagnostics[index].c_str(), RGB(224, 220, 211), 10);
    }
    if (document.diagnostics.size() > count && count > 0U) {
        const std::string more = "+" + std::to_string(document.diagnostics.size() - count) + " more";
        DrawText(dc, RECT{ layout.diagnosticsPanel.left + 10, layout.diagnosticsPanel.bottom - 24, layout.diagnosticsPanel.right - 10, layout.diagnosticsPanel.bottom - 6 }, more.c_str(), RGB(168, 159, 145), 10, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawDetailsPanel(HDC dc, const MaterialEditorPanelLayout& layout, const MaterialEditorPanelDetailsRows& rows) {
    if (RectWidth(layout.detailsPanel) < 220 || RectHeight(layout.detailsPanel) < 140) {
        return;
    }

    DrawGraphOverlay(dc, layout.detailsPanel, RGB(22, 25, 29), RGB(54, 61, 71));
    DrawText(dc, RECT{ layout.detailsPanel.left + 10, layout.detailsPanel.top + 8, layout.detailsPanel.right - 10, layout.detailsPanel.top + 30 }, rows.title.c_str(), RGB(235, 238, 243), 12, FW_SEMIBOLD);

    int y = layout.detailsPanel.top + 34;
    const int rowHeight = 18;
    const int bottom = layout.detailsPanel.bottom - 10;
    {
        const RECT searchRect{
            layout.detailsPanel.left + 10,
            y,
            layout.detailsPanel.right - 10,
            y + 24,
        };
        FillRoundedRect(dc, searchRect, rows.findFocused ? RGB(34, 45, 55) : RGB(30, 34, 40), 4);
        StrokeRoundedRect(dc, searchRect, rows.findFocused ? RGB(83, 128, 165) : RGB(62, 70, 82), 4);
        const std::string searchText = rows.findQuery.empty() ? "Find in material" : rows.findQuery;
        DrawText(
            dc,
            RECT{ searchRect.left + 8, searchRect.top, searchRect.right - 8, searchRect.bottom },
            searchText.c_str(),
            rows.findQuery.empty() ? RGB(151, 162, 176) : RGB(232, 237, 243),
            9,
            rows.findFocused ? FW_SEMIBOLD : FW_NORMAL,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        y += 32;
    }

    if (!rows.instanceParentRows.empty()) {
        DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Parent Chain", RGB(190, 169, 239), 10, FW_SEMIBOLD);
        y += 22;
        for (const MaterialEditorInstanceParentChainRow& row : rows.instanceParentRows) {
            if (y + rowHeight > bottom) {
                break;
            }
            const std::string text = (row.current ? "* " : "  ") + row.label;
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, text.c_str(), row.current ? RGB(234, 225, 255) : RGB(201, 193, 222), 9, row.current ? FW_SEMIBOLD : FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
        }
        y += 6;
    }

    if (!rows.instanceOverrideGroupRows.empty()) {
        DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Instance Overrides", RGB(126, 201, 143), 10, FW_SEMIBOLD);
        y += 22;
        for (const MaterialEditorInstanceOverrideGroupRow& group : rows.instanceOverrideGroupRows) {
            if (y + rowHeight > bottom) {
                break;
            }
            const std::string text =
                std::string{ group.expanded ? "v " : "> " } +
                MaterialEditorPanelParameterGroupName(group.group) + "  " +
                std::to_string(group.activeOverrideCount) + "/" + std::to_string(group.totalParameterCount) + " overrides";
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, text.c_str(), RGB(198, 222, 205), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
        }
        y += 6;
    }

    if (!rows.instanceStaticSwitchRows.empty()) {
        DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Static Switches", RGB(231, 183, 118), 10, FW_SEMIBOLD);
        y += 22;
        for (const MaterialEditorInstanceStaticSwitchRow& row : rows.instanceStaticSwitchRows) {
            if (y + rowHeight > bottom) {
                break;
            }
            const std::string text = row.displayName + " = " + row.value + (row.overrideActive ? " override" : " parent");
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, text.c_str(), row.overrideActive ? RGB(245, 215, 174) : RGB(205, 194, 181), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
        }
        y += 6;
    }

    if (!rows.layerTreeRows.empty()) {
        DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Layer Stack", RGB(157, 198, 241), 10, FW_SEMIBOLD);
        y += 22;
        for (const MaterialEditorLayerTreeRow& row : rows.layerTreeRows) {
            if (y + rowHeight > bottom) {
                break;
            }
            const std::string name = row.layerName.empty() ? ("Layer " + std::to_string(row.index + 1U)) : row.layerName;
            const std::string text =
                std::string{ row.enabled ? "[on] " : "[off] " } + name +
                " L:" + std::to_string(row.layerFunctionAssetId) +
                (row.index > 0U ? (" B:" + std::to_string(row.blendFunctionAssetId)) : std::string{}) +
                " params " + std::to_string(row.layerParameterCount + row.blendParameterCount);
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, text.c_str(), row.enabled ? RGB(205, 219, 238) : RGB(141, 151, 164), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
        }
        y += 6;
    }

    if (!rows.findResults.empty()) {
        DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Find", RGB(180, 213, 154), 10, FW_SEMIBOLD);
        y += 22;
        std::size_t resultCount = 0U;
        for (const MaterialEditorFindResult& result : rows.findResults) {
            if (y + rowHeight > bottom || resultCount >= 5U) {
                break;
            }
            const std::string text = result.label + "  " + result.detail;
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, text.c_str(), RGB(205, 226, 194), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
            ++resultCount;
        }
        y += 6;
    }

    if (!rows.nodePropertyRows.empty()) {
        DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Node Properties", RGB(157, 198, 241), 10, FW_SEMIBOLD);
        y += 22;
        for (const MaterialEditorGraphNodeProperty& property : rows.nodePropertyRows) {
            if (y + MaterialEditorPanelMetrics::DetailsNodePropertyRowHeight > bottom) {
                break;
            }
            const RECT row{
                layout.detailsPanel.left + 12,
                y,
                layout.detailsPanel.right - 12,
                y + MaterialEditorPanelMetrics::DetailsNodePropertyRowHeight,
            };
            const RECT label{
                row.left,
                row.top,
                row.left + std::min(122, std::max(72, RectWidth(row) / 2)),
                row.bottom,
            };
            const RECT field{
                label.right + 8,
                row.top + 2,
                row.right,
                row.bottom - 2,
            };
            DrawText(dc, label, property.displayName.c_str(), RGB(198, 205, 218), 9, FW_NORMAL, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            FillRoundedRect(dc, field, RGB(31, 35, 41), 4);
            StrokeRoundedRect(dc, field, RGB(67, 76, 91), 4);

            std::string valueText = MaterialEditorPanelParameterValueText(property.value);
            if (property.kind == MaterialEditorGraphNodePropertyKind::Enum) {
                const auto selected = std::ranges::find_if(property.options, [&property](const MaterialEditorGraphNodePropertyOption& option) {
                    return option.value == property.value.text;
                });
                if (selected != property.options.end()) {
                    valueText = selected->label;
                }
            }
            if (property.kind == MaterialEditorGraphNodePropertyKind::TextureAsset) {
                valueText += "  Pick";
            }

            if (property.kind == MaterialEditorGraphNodePropertyKind::Numeric) {
                float normalized = property.value.numbers[0];
                if (property.range.has_value()) {
                    const float span = std::max(0.0001F, property.range->max - property.range->min);
                    normalized = (property.value.numbers[0] - property.range->min) / span;
                }
                normalized = std::clamp(normalized, 0.0F, 1.0F);
                const RECT fill{
                    field.left + 1,
                    field.top + 1,
                    field.left + 1 + static_cast<int>(std::round(static_cast<float>(std::max(0, RectWidth(field) - 2)) * normalized)),
                    field.bottom - 1,
                };
                if (fill.right > fill.left) {
                    FillRoundedRect(dc, fill, RGB(55, 107, 142), 4);
                }
            } else if (property.kind == MaterialEditorGraphNodePropertyKind::Color) {
                const RECT swatch{
                    field.left + 5,
                    field.top + 4,
                    field.left + 25,
                    field.bottom - 4,
                };
                const COLORREF color = RGB(
                    std::clamp(static_cast<int>(property.value.numbers[0] * 255.0F), 0, 255),
                    std::clamp(static_cast<int>(property.value.numbers[1] * 255.0F), 0, 255),
                    std::clamp(static_cast<int>(property.value.numbers[2] * 255.0F), 0, 255));
                FillRoundedRect(dc, swatch, color, 4);
                StrokeRoundedRect(dc, swatch, RGB(18, 18, 18), 4);
            } else if (property.kind == MaterialEditorGraphNodePropertyKind::Enum) {
                DrawText(dc, RECT{ field.right - 22, field.top, field.right - 5, field.bottom }, "v", RGB(160, 176, 194), 9, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            const int textLeft = property.kind == MaterialEditorGraphNodePropertyKind::Color ? field.left + 32 : field.left + 8;
            DrawText(dc, RECT{ textLeft, field.top, field.right - 22, field.bottom }, valueText.c_str(), RGB(229, 232, 238), 9, FW_NORMAL, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            y += MaterialEditorPanelMetrics::DetailsNodePropertyRowHeight;

            if (property.kind == MaterialEditorGraphNodePropertyKind::Enum && property.dropdownOpen) {
                for (const MaterialEditorGraphNodePropertyOption& option : property.options) {
                    if (y + MaterialEditorPanelMetrics::DetailsNodePropertyOptionHeight > bottom) {
                        break;
                    }
                    const bool active = option.value == property.value.text;
                    const RECT optionRow{
                        layout.detailsPanel.left + 28,
                        y,
                        layout.detailsPanel.right - 18,
                        y + MaterialEditorPanelMetrics::DetailsNodePropertyOptionHeight,
                    };
                    FillRoundedRect(dc, optionRow, active ? RGB(48, 83, 109) : RGB(27, 31, 37), 4);
                    DrawText(dc, RECT{ optionRow.left + 8, optionRow.top, optionRow.right - 8, optionRow.bottom }, option.label.c_str(), active ? RGB(239, 246, 252) : RGB(202, 211, 222), 9, active ? FW_SEMIBOLD : FW_NORMAL, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                    y += MaterialEditorPanelMetrics::DetailsNodePropertyOptionHeight;
                }
            }
        }
        y += 6;
    }

    if (!rows.materialDiffRows.empty()) {
        DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Material Diff", RGB(241, 185, 126), 10, FW_SEMIBOLD);
        y += 22;
        std::size_t diffCount = 0U;
        for (const std::string& row : rows.materialDiffRows) {
            if (y + rowHeight > bottom || diffCount >= 8U) {
                break;
            }
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, row.c_str(), RGB(235, 215, 190), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
            ++diffCount;
        }
        if (diffCount < rows.materialDiffRows.size() && y + rowHeight <= bottom) {
            const std::string more = "+" + std::to_string(rows.materialDiffRows.size() - diffCount) + " more material changes";
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, more.c_str(), RGB(201, 178, 150), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
        }
        y += 6;
    }

    if (!rows.debugChannelRows.empty()) {
        DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Debug Channels", RGB(239, 203, 127), 10, FW_SEMIBOLD);
        y += 22;
        std::size_t debugCount = 0U;
        for (const MaterialDebugChannelRow& row : rows.debugChannelRows) {
            if (y + rowHeight > bottom || debugCount >= 6U) {
                break;
            }
            const std::string text = row.label + "  " + row.value;
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, text.c_str(), RGB(224, 218, 203), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
            ++debugCount;
        }
        y += 6;
    }

    if (y + 20 <= bottom) {
        DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Parameters", RGB(126, 201, 143), 10, FW_SEMIBOLD);
        y += 22;
    }
    std::size_t parameterCount = 0U;
    for (const std::string& row : rows.parameterRows) {
        if (y + rowHeight > bottom || parameterCount >= 7U) {
            break;
        }
        DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, row.c_str(), RGB(198, 205, 218), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
        y += rowHeight;
        ++parameterCount;
    }

    if (y + 28 <= bottom) {
        y += 6;
        DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Texture Slots", RGB(126, 181, 223), 10, FW_SEMIBOLD);
        y += 22;
    }
    std::size_t slotCount = 0U;
    for (const std::string& row : rows.textureSlotRows) {
        if (y + rowHeight > bottom || slotCount >= 6U) {
            break;
        }
        DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, row.c_str(), RGB(198, 205, 218), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
        y += rowHeight;
        ++slotCount;
    }

    if (rows.materialStats.available || !rows.materialStats.warnings.empty()) {
        if (y + 28 <= bottom) {
            y += 6;
            DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Material Stats", RGB(241, 208, 130), 10, FW_SEMIBOLD);
            y += 22;
        }
        for (const MaterialEditorMaterialStatsPassRow& row : rows.materialStats.passRows) {
            if (y + rowHeight > bottom) {
                break;
            }
            const std::string text =
                row.passName + (row.graphProgram ? " graph" : " builtin") +
                " inst " + std::to_string(row.instructionEstimate) +
                " tex " + std::to_string(row.textureSampleCount) + "/" + std::to_string(row.samplerCount) +
                " uni " + std::to_string(row.uniformCount) +
                " var " + std::to_string(row.varyingCount) +
                " variants " + std::to_string(row.staticVariantCount);
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, text.c_str(), RGB(228, 215, 184), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
        }
        std::size_t warningCount = 0U;
        for (const std::string& warning : rows.materialStats.warnings) {
            if (warningCount >= 3U || y + rowHeight > bottom) {
                break;
            }
            const std::string text = "! " + warning;
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, text.c_str(), RGB(244, 187, 121), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
            ++warningCount;
        }
    }

    if (rows.shaderViewer.available || !rows.shaderViewer.warnings.empty()) {
        if (y + 28 <= bottom) {
            y += 6;
            DrawText(dc, RECT{ layout.detailsPanel.left + 10, y, layout.detailsPanel.right - 10, y + 20 }, "Shader Viewer", RGB(177, 205, 246), 10, FW_SEMIBOLD);
            y += 22;
        }
        std::size_t sourceCount = 0U;
        for (const MaterialEditorShaderSourceView& source : rows.shaderViewer.sources) {
            if (y + rowHeight > bottom || sourceCount >= 3U) {
                break;
            }
            const std::string text =
                source.passName + " " + source.stageName + " " + source.backendName +
                " lines " + std::to_string(std::count(source.source.begin(), source.source.end(), '\n') + 1U);
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, text.c_str(), RGB(204, 219, 241), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
            ++sourceCount;
        }
        std::size_t reflectionCount = 0U;
        for (const MaterialEditorShaderReflectionRow& row : rows.shaderViewer.reflectionRows) {
            if (y + rowHeight > bottom || reflectionCount >= 4U) {
                break;
            }
            const std::string text = row.category + " " + row.name + (row.stableId.empty() ? "" : (" [" + row.stableId + "]"));
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, text.c_str(), RGB(192, 205, 225), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
            ++reflectionCount;
        }
        std::size_t warningCount = 0U;
        for (const std::string& warning : rows.shaderViewer.warnings) {
            if (warningCount >= 2U || y + rowHeight > bottom) {
                break;
            }
            const std::string text = "! " + warning;
            DrawText(dc, RECT{ layout.detailsPanel.left + 12, y, layout.detailsPanel.right - 12, y + rowHeight }, text.c_str(), RGB(244, 187, 121), 9, FW_NORMAL, DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
            ++warningCount;
        }
    }
}

void DrawMaterialContent(HDC dc, const RECT& content, const EditorSceneContext& sceneContext, const kb::assets::AssetMetadata& metadata) {
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    const std::optional<MaterialEditorDocumentView> document = ReadDocumentView(sceneContext, metadata);
    const EditorMaterialPreviewTelemetry telemetry = sceneContext.MaterialPreviewTelemetry();

    if (!document.has_value()) {
        DrawGraphGrid(dc, layout.graphCanvas);
        DrawText(dc, layout.graphCanvas, "Material document could not be read.", RGB(232, 112, 112), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    if (document->material.has_value()) {
        DrawGraphCanvas(dc, content, *document->material, sceneContext, metadata.id, sceneContext.SelectedMaterialGraphNodeId());
    } else {
        DrawGraphGrid(dc, layout.graphCanvas);
        DrawText(dc, layout.graphCanvas, "Material document could not be parsed.", RGB(232, 112, 112), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    const EditorMaterialGraphCookBanner cookBanner =
        MakeEditorMaterialGraphCookBanner(sceneContext.OpenMaterialGraphCookResult().status);
    DrawPreviewOverlay(dc, layout, telemetry, cookBanner);
    DrawDiagnosticsPanel(dc, layout, *document);
    if (sceneContext.MaterialEditor().InfoPanelVisible()) {
        const std::vector<MaterialEditorGraphNodeProperty> nodeProperties =
            metadata.type == "RenderMaterialInstance"
                ? std::vector<MaterialEditorGraphNodeProperty>{}
                : sceneContext.MaterialEditor().GraphNodeProperties(sceneContext.MaterialEditor().SelectedNodeId());
        MaterialEditorPanelDetailsRows details = MaterialEditorPanelRenderer::DetailsRows(
            sceneContext.MaterialEditor().Parameters(),
            sceneContext.MaterialEditor().SelectedNodeId(),
            nodeProperties);
        if (sceneContext.MaterialEditor().SelectedNodeId() != 0U) {
            details.title = sceneContext.MaterialEditor().GraphNodeDisplayName(sceneContext.MaterialEditor().SelectedNodeId());
        }
        details.instanceParentRows = sceneContext.MaterialEditor().InstanceParentChainRows();
        details.instanceOverrideGroupRows = sceneContext.MaterialEditor().InstanceOverrideGroups();
        details.instanceStaticSwitchRows = sceneContext.MaterialEditor().InstanceStaticSwitchRows();
        details.layerTreeRows = sceneContext.MaterialEditor().LayerTreeRows();
        details.materialStats = sceneContext.MaterialEditor().MaterialStats();
        details.shaderViewer = sceneContext.MaterialEditor().ShaderViewer();
        details.findQuery = std::string{ sceneContext.MaterialEditor().FindQuery() };
        details.findFocused = sceneContext.MaterialEditor().IsFindFocused();
        details.findResults = sceneContext.MaterialEditor().FindResults();
        details.materialDiffRows = sceneContext.MaterialEditor().MaterialDiffRows();
        if (metadata.type == "RenderMaterialInstance") {
            details.title = "Material Instance Overrides";
        }
        if (document->material.has_value()) {
            details.debugChannelRows = MaterialAssetFormatter::DebugChannelRows(document->material->desc, metadata.id.value);
        }
        DrawDetailsPanel(
            dc,
            layout,
            details);
    }
}

} // namespace

void MaterialEditorPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    static_cast<void>(theme);
    GdiDrawing::FillRectColor(dc, content, RGB(26, 28, 31));
    DrawHeader(dc, content, sceneContext, sceneContext.HasDirtyMaterialAssetEdit(), sceneContext.MaterialEditor().InfoPanelVisible());

    const kb::assets::AssetId assetId = sceneContext.MaterialEditor().OpenAssetId();
    const kb::assets::AssetMetadata* metadata = assetId.IsValid()
        ? sceneContext.Scene().Assets().Manager().Registry().Find(assetId)
        : nullptr;

    if (metadata == nullptr || !IsMaterialDocument(*metadata)) {
        const RECT body{ content.left, content.top + kHeaderHeight, content.right, content.bottom };
        DrawText(dc, body, "Double-click a Material (.kbmat) or Material Instance (.kbmatinst) in Project Files to edit it here.", RGB(86, 92, 100), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        return;
    }

    DrawMaterialContent(dc, content, sceneContext, *metadata);
}

std::optional<RECT> MaterialEditorPanelRenderer::MaterialPreviewRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    const kb::assets::AssetId assetId = sceneContext.MaterialEditor().OpenAssetId();
    if (!assetId.IsValid()) {
        return std::nullopt;
    }
    const kb::assets::AssetMetadata* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr || !IsMaterialDocument(*metadata)) {
        return std::nullopt;
    }
    const RECT frame = PreviewFrameRect(content);
    return RECT{ frame.left + 1, frame.top + 1, frame.right - 1, frame.bottom - 1 };
}

} // namespace kb::editor

#endif
