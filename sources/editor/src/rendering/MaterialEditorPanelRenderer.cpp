#include "rendering/MaterialEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "platform/win32/EditorMaterialAssetPickerDialog.hpp"
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
#include <cctype>
#include <cstdlib>
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
constexpr int kGraphNodeCornerDiameter = 14;
constexpr int kGraphTitleFontSize = 11;
constexpr int kGraphPinFontSize = 10;
constexpr int kGraphMinTextPointSize = 9;

namespace MaterialGraphTheme {
constexpr COLORREF NodeBody = RGB(23, 25, 30);
constexpr COLORREF NodeBodyBottom = RGB(23, 25, 30);
constexpr COLORREF NodePanel = RGB(50, 54, 62);
constexpr COLORREF NodePanelBorder = RGB(0, 0, 0);
constexpr COLORREF NodeOutline = RGB(0, 0, 0);
constexpr COLORREF NodeOutlineSelected = RGB(73, 221, 210);
constexpr COLORREF NodeShadow = RGB(0, 0, 0);
constexpr COLORREF NodeHeader = RGB(56, 77, 112);
constexpr COLORREF NodeHeaderBottom = RGB(40, 55, 81);
constexpr COLORREF Text = RGB(236, 241, 247);
constexpr COLORREF TextMuted = RGB(174, 181, 191);
constexpr COLORREF Field = RGB(13, 15, 22);
constexpr COLORREF FieldBorder = RGB(0, 0, 0);
constexpr COLORREF FieldFocus = RGB(92, 158, 245);
constexpr COLORREF LinkShadow = RGB(0, 0, 0);
constexpr COLORREF LinkFallback = RGB(164, 176, 190);
constexpr COLORREF Canvas = RGB(17, 18, 22);
constexpr COLORREF GridLineMinor = RGB(25, 26, 30);
constexpr COLORREF GridLineMajor = RGB(35, 36, 40);
} // namespace MaterialGraphTheme

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
    DrawCommandButton(dc, layout.previewNormalButton, "Normal", sceneContext.MaterialPreviewNormalDebugViewEnabled());
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

[[nodiscard]] std::optional<RECT> IntersectRectOptional(const RECT& lhs, const RECT& rhs) noexcept {
    RECT result{};
    if (IntersectRect(&result, &lhs, &rhs) == 0) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] bool RectContainsRect(const RECT& outer, const RECT& inner) noexcept {
    return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right && inner.bottom <= outer.bottom;
}

[[nodiscard]] int ScaleMetric(int value, float scale) noexcept {
    return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * scale)));
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
        return { { "xyz", "RGB" }, { "r", "R" }, { "g", "G" }, { "b", "B" } };
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
        return "Scalar";
    case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
        return "Bool";
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
        return "XY";
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
        return "RGB Node";
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
        return "RGBA Node";
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
// Graph-paper backdrop: a solid canvas fill plus subtle minor/major lines that
// pan and scale with graph world coordinates.
void DrawGraphGrid(HDC dc, const RECT& canvas, float zoom = 1.0F, int panX = 0, int panY = 0) {
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, canvas.left, canvas.top, canvas.right, canvas.bottom);

    GdiDrawing::FillRectColor(dc, canvas, MaterialGraphTheme::Canvas);

    constexpr int kWorldSpacing = 32;
    const int spacing = ScaleMetric(kWorldSpacing, zoom);
    if (spacing < 7) {
        RestoreDC(dc, savedDc);
        return;
    }

    const int startX = canvas.left + ((panX % spacing) + spacing) % spacing;
    const int startY = canvas.top + ((panY % spacing) + spacing) % spacing;
    const int majorWidth = std::max(1, static_cast<int>(std::round(1.4F * std::clamp(zoom, 0.75F, 1.5F))));

    int verticalCount = 0;
    for (int x = startX; x < canvas.right && verticalCount < 600; x += spacing, ++verticalCount) {
        const int worldIndex = (x - canvas.left - panX) / std::max(1, spacing);
        const bool major = worldIndex % 4 == 0;
        const int width = major ? majorWidth : 1;
        GdiDrawing::FillRectColor(
            dc,
            RECT{ x, canvas.top, std::min<LONG>(canvas.right, x + width), canvas.bottom },
            major ? MaterialGraphTheme::GridLineMajor : MaterialGraphTheme::GridLineMinor);
    }

    int horizontalCount = 0;
    for (int y = startY; y < canvas.bottom && horizontalCount < 600; y += spacing, ++horizontalCount) {
        const int worldIndex = (y - canvas.top - panY) / std::max(1, spacing);
        const bool major = worldIndex % 4 == 0;
        const int height = major ? majorWidth : 1;
        GdiDrawing::FillRectColor(
            dc,
            RECT{ canvas.left, y, canvas.right, std::min<LONG>(canvas.bottom, y + height) },
            major ? MaterialGraphTheme::GridLineMajor : MaterialGraphTheme::GridLineMinor);
    }

    RestoreDC(dc, savedDc);
}
// ---------------------------------------------------------------------------------------------

[[nodiscard]] float GraphBezierHandleDistance(POINT from, POINT to) noexcept {
    const float distanceX = static_cast<float>(std::abs(static_cast<int>(to.x - from.x)));
    const float distanceY = static_cast<float>(std::abs(static_cast<int>(to.y - from.y)));
    const float distance = std::sqrt((distanceX * distanceX) + (distanceY * distanceY));
    const float naturalHandle = std::max(distanceX * 0.5F, distance * 0.18F);
    return std::clamp(naturalHandle, 10.0F, 96.0F);
}

[[nodiscard]] POINT GraphCanvasPointToPoint(MaterialGraphCanvasPoint point) noexcept {
    return POINT{ static_cast<LONG>(std::lround(point.x)), static_cast<LONG>(std::lround(point.y)) };
}

[[nodiscard]] float GraphCanvasBezierHandleDistance(POINT from, POINT to, float zoom) noexcept {
    const float distanceX = static_cast<float>(std::abs(static_cast<int>(to.x - from.x)));
    return std::max(48.0F * std::clamp(zoom, 0.25F, 2.0F), distanceX * 0.5F);
}

[[nodiscard]] std::optional<MaterialGraphCanvasPoint> GraphCanvasPinWindowPoint(
    const MaterialGraphCanvas& canvas,
    std::uint32_t nodeId,
    std::string_view pinName,
    bool output) {
    const std::string stableNodeId = std::to_string(nodeId);
    const std::vector<MaterialGraphCanvasNode>& nodes = canvas.Nodes();
    for (std::size_t nodeIndex = 0U; nodeIndex < nodes.size(); ++nodeIndex) {
        const MaterialGraphCanvasNode& node = nodes[nodeIndex];
        if (node.stableId != stableNodeId) {
            continue;
        }
        const std::vector<MaterialGraphCanvasPin>& pins = output ? node.outputs : node.inputs;
        for (std::size_t pinIndex = 0U; pinIndex < pins.size(); ++pinIndex) {
            if (pins[pinIndex].stableId == pinName) {
                return canvas.PinCenterWindow(
                    static_cast<std::uint32_t>(nodeIndex),
                    static_cast<std::uint32_t>(pinIndex),
                    output);
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// Takes an already-configured Gdiplus::Graphics (see DrawGraphPin/FillRoundedRectAlpha above for
// why): a graph with many links used to construct one Gdiplus::Graphics per link, per repaint.
void DrawGraphBezier(Gdiplus::Graphics& graphics, POINT from, POINT to, COLORREF color, int width, float handleDistance = -1.0F) {
    Gdiplus::Pen shadowPen(ToGdiplusColor(MaterialGraphTheme::LinkShadow, 96U), static_cast<Gdiplus::REAL>(std::max(1, width + 2)));
    Gdiplus::Pen haloPen(ToGdiplusColor(color, 50U), static_cast<Gdiplus::REAL>(std::max(1, width + 1)));
    Gdiplus::Pen pen(ToGdiplusColor(color, 232U), static_cast<Gdiplus::REAL>(std::max(1, width)));
    shadowPen.SetStartCap(Gdiplus::LineCapRound);
    shadowPen.SetEndCap(Gdiplus::LineCapRound);
    haloPen.SetStartCap(Gdiplus::LineCapRound);
    haloPen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const float dx = handleDistance > 0.0F ? handleDistance : GraphBezierHandleDistance(from, to);
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
    int y) {
    const kb::render::RenderMaterialGraphDocument defaultGraph = graph.nodes.empty()
        ? kb::render::MakeDefaultRenderMaterialGraphDocument()
        : kb::render::RenderMaterialGraphDocument{};
    const kb::render::RenderMaterialGraphDocument& graphView = graph.nodes.empty() ? defaultGraph : graph;
    const MaterialEditorPanelLayout layout = ResolveLayout(content);
    if (!MaterialEditorPanelPointInRect(layout.graphCanvas, x, y)) {
        return std::nullopt;
    }

    MaterialGraphCanvasDocumentBuildResult canvasResult = MaterialEditorPanelBuildInteractiveGraphCanvas(content, graphView, sceneContext, assetId);
    const std::optional<std::uint32_t> canvasLink = canvasResult.canvas.HitTestLink(static_cast<float>(x), static_cast<float>(y));
    if (!canvasLink.has_value()) {
        return std::nullopt;
    }
    const std::vector<MaterialGraphCanvasLink>& links = canvasResult.canvas.Links();
    if (*canvasLink >= links.size()) {
        return std::nullopt;
    }
    const std::string& stableLinkId = links[*canvasLink].stableId;
    for (const kb::render::RenderMaterialGraphLink& link : graphView.links) {
        if (stableLinkId == std::to_string(link.id)) {
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
constexpr int kGraphPinSpritePadding = 5;

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

    const COLORREF edge = RGB(4, 6, 9);
    const COLORREF outer = ScaleColor(color, tinted ? 0.72F : 0.9F);
    const COLORREF face = tinted ? ScaleColor(color, 1.08F) : ScaleColor(color, 1.16F);
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
    Gdiplus::SolidBrush shadowBrush(ToGdiplusColor(RGB(0, 0, 0), 118U));
    Gdiplus::SolidBrush edgeBrush(ToGdiplusColor(edge));
    Gdiplus::SolidBrush outerBrush(ToGdiplusColor(outer));
    Gdiplus::SolidBrush faceBrush(ToGdiplusColor(face));
    Gdiplus::SolidBrush socketBrush(ToGdiplusColor(RGB(18, 22, 28), 255U));
    Gdiplus::SolidBrush shineBrush(ToGdiplusColor(RGB(255, 255, 255), 50U));
    Gdiplus::RectF shadowRect = outerRect;
    shadowRect.X += std::max(1.0F, insetScale);
    shadowRect.Y += std::max(1.0F, insetScale);
    graphics.FillEllipse(&shadowBrush, shadowRect);
    graphics.FillEllipse(&edgeBrush, outerRect);
    graphics.FillEllipse(&socketBrush, innerRect);
    const float ringInset = std::max(1.0F, inset * 1.25F);
    const Gdiplus::RectF ringRect{
        outerRect.X + ringInset,
        outerRect.Y + ringInset,
        std::max(1.0F, outerRect.Width - (ringInset * 2.0F)),
        std::max(1.0F, outerRect.Height - (ringInset * 2.0F)),
    };
    graphics.FillEllipse(&outerBrush, ringRect);
    const float coreInset = std::max(2.0F, inset * 1.85F);
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
    bool tinted = false) {
    static_cast<void>(tinted);
    const int r = std::max(4, ScaleMetric(kGraphNodePinRadius, scale));
    const Gdiplus::RectF outerRect{
        static_cast<float>(point.x - r),
        static_cast<float>(point.y - r),
        static_cast<float>(r * 2),
        static_cast<float>(r * 2),
    };
    Gdiplus::SolidBrush faceBrush(ToGdiplusColor(color));
    Gdiplus::Pen edgePen(ToGdiplusColor(RGB(0, 0, 0), 154U), std::max<Gdiplus::REAL>(1.0F, scale));
    graphics.FillEllipse(&faceBrush, outerRect);
    graphics.DrawEllipse(&edgePen, outerRect);
}

[[nodiscard]] COLORREF GraphPinTypeColor(kb::render::RenderMaterialGraphPinType type) noexcept {
    static_cast<void>(type);
    return RGB(219, 224, 235);
}

[[nodiscard]] COLORREF GraphInputPinColor(const kb::render::RenderMaterialGraphNode& node, std::string_view pin) noexcept {
    return GraphPinTypeColor(kb::render::RenderMaterialGraphPinDataType(node, pin, false));
}

[[nodiscard]] COLORREF GraphOutputPinLabelColor(kb::render::RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    if (!MaterialEditorPanelIsTextureSamplePreviewNode(kind)) {
        return MaterialGraphTheme::TextMuted;
    }
    static_cast<void>(pin);
    return MaterialGraphTheme::Text;
}

[[nodiscard]] std::optional<COLORREF> GraphChannelPinColor(std::string_view pin) noexcept {
    if (pin == "r") {
        return RGB(255, 36, 43);
    }
    if (pin == "g") {
        return RGB(36, 216, 62);
    }
    if (pin == "b") {
        return RGB(36, 118, 255);
    }
    return std::nullopt;
}

[[nodiscard]] bool GraphNodeUsesChannelPinColors(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    return MaterialEditorPanelIsTextureSamplePreviewNode(kind) ||
        kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector ||
        kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor ||
        kind == kb::render::RenderMaterialGraphNodeKind::ParameterColor ||
        kind == kb::render::RenderMaterialGraphNodeKind::CollectionParameter;
}

[[nodiscard]] COLORREF GraphOutputPinColor(kb::render::RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    if (GraphNodeUsesChannelPinColors(kind)) {
        if (const std::optional<COLORREF> color = GraphChannelPinColor(pin)) {
            return *color;
        }
    }
    return GraphPinTypeColor(kb::render::RenderMaterialGraphPinDataType(kind, pin, true));
}

[[nodiscard]] COLORREF GraphOutputPinColor(const kb::render::RenderMaterialGraphNode& node, std::string_view pin) noexcept {
    if (GraphNodeUsesChannelPinColors(node.kind)) {
        if (const std::optional<COLORREF> color = GraphChannelPinColor(pin)) {
            return *color;
        }
    }
    return GraphPinTypeColor(kb::render::RenderMaterialGraphPinDataType(node, pin, true));
}

[[nodiscard]] bool GraphOutputPinTinted(kb::render::RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    return GraphNodeUsesChannelPinColors(kind) &&
        (pin == "r" || pin == "g" || pin == "b");
}

[[nodiscard]] COLORREF GraphNodeAccentColor(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    if (MaterialEditorPanelIsTexturePreviewNode(kind)) {
        return RGB(87, 66, 117);
    }
    if (kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector ||
        kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor ||
        kind == kb::render::RenderMaterialGraphNodeKind::ParameterColor ||
        kind == kb::render::RenderMaterialGraphNodeKind::ColorRamp ||
        kind == kb::render::RenderMaterialGraphNodeKind::HsvToRgb ||
        kind == kb::render::RenderMaterialGraphNodeKind::RgbToHsv) {
        return RGB(102, 87, 51);
    }
    if (MaterialEditorPanelIsConstantNode(kind) ||
        kind == kb::render::RenderMaterialGraphNodeKind::ParameterScalar ||
        kind == kb::render::RenderMaterialGraphNodeKind::ParameterVector ||
        kind == kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter ||
        kind == kb::render::RenderMaterialGraphNodeKind::CollectionParameter) {
        return RGB(56, 92, 71);
    }
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
    case kb::render::RenderMaterialGraphNodeKind::MakeMaterialAttributes:
    case kb::render::RenderMaterialGraphNodeKind::BreakMaterialAttributes:
    case kb::render::RenderMaterialGraphNodeKind::GetMaterialAttributes:
    case kb::render::RenderMaterialGraphNodeKind::SetMaterialAttributes:
    case kb::render::RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        return RGB(117, 66, 66);
    case kb::render::RenderMaterialGraphNodeKind::CustomCode:
    case kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall:
    case kb::render::RenderMaterialGraphNodeKind::FunctionInput:
    case kb::render::RenderMaterialGraphNodeKind::FunctionOutput:
        return RGB(51, 87, 102);
    case kb::render::RenderMaterialGraphNodeKind::QualitySwitch:
    case kb::render::RenderMaterialGraphNodeKind::FeatureLevelSwitch:
    case kb::render::RenderMaterialGraphNodeKind::ShadingPathSwitch:
    case kb::render::RenderMaterialGraphNodeKind::ShaderStageSwitch:
    case kb::render::RenderMaterialGraphNodeKind::RuntimeSwitch:
        return RGB(56, 77, 112);
    case kb::render::RenderMaterialGraphNodeKind::Reroute:
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage:
    case kb::render::RenderMaterialGraphNodeKind::CompositeInput:
    case kb::render::RenderMaterialGraphNodeKind::CompositeOutput:
        return RGB(72, 72, 77);
    default:
        return RGB(56, 77, 112);
    }
}

[[nodiscard]] COLORREF GraphNodeHeaderColor(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    return GraphNodeAccentColor(kind);
}

void DrawGraphNodeFrame(HDC dc, Gdiplus::Graphics& graphics, const RECT& rect, COLORREF body, float scale) {
    const int cornerDiameter = ScaleMetric(kGraphNodeCornerDiameter, scale);
    for (int layer = 3; layer >= 1; --layer) {
        const int spread = std::max(1, ScaleMetric(static_cast<int>(std::round(static_cast<float>(layer) * 2.2F)), scale));
        const int drop = std::max(1, ScaleMetric(static_cast<int>(std::round(static_cast<float>(layer) * 1.7F)), scale));
        const BYTE alpha = static_cast<BYTE>(std::clamp(52 / layer, 10, 52));
        FillRoundedRectAlpha(
            graphics,
            RECT{ rect.left - spread, rect.top - spread + drop, rect.right + spread, rect.bottom + spread + drop },
            MaterialGraphTheme::NodeShadow,
            alpha,
            cornerDiameter + (spread * 2));
    }
    FillRoundedRect(dc, rect, body, ScaleMetric(kGraphNodeCornerDiameter, scale));
}

[[nodiscard]] RECT GraphNodePaintBounds(const RECT& rect, float scale) noexcept {
    const int outerSpread = ScaleMetric(12, scale);
    const int contactDrop = ScaleMetric(8, scale);
    const int contactSpread = ScaleMetric(4, scale);
    const int pinSpriteExtent = std::max(4, ScaleMetric(kGraphNodePinRadius, scale)) + kGraphPinSpritePadding + ScaleMetric(2, scale);
    const int horizontal = std::max(outerSpread, pinSpriteExtent);
    const int top = std::max(outerSpread, pinSpriteExtent);
    const int bottom = std::max({ outerSpread, contactDrop + contactSpread, pinSpriteExtent });
    return RECT{
        rect.left - horizontal,
        rect.top - top,
        rect.right + horizontal,
        rect.bottom + bottom,
    };
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
    StrokeRoundedRect(dc, rect, selected ? MaterialGraphTheme::NodeOutlineSelected : ScaleColor(color, 1.28F), cornerDiameter, selected ? 2 : 1);
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

[[nodiscard]] bool TextureNodeDisplayNameIsGenerated(const kb::render::RenderMaterialGraphNode& node) {
    const std::string id = std::to_string(node.id);
    switch (node.kind) {
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return node.parameter.displayName == "Texture " + id;
    case kb::render::RenderMaterialGraphNodeKind::TextureObject:
        return node.parameter.displayName == "Texture Object " + id;
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectCube:
        return node.parameter.displayName == "Texture Object Cube " + id;
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume:
        return node.parameter.displayName == "Texture Object Volume " + id;
    case kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray:
        return node.parameter.displayName == "Texture Object 2D Array " + id ||
            node.parameter.displayName == "Texture Object Array " + id;
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return node.parameter.displayName == "Texture Sample " + id;
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleCube:
        return node.parameter.displayName == "Texture Sample Cube " + id;
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume:
        return node.parameter.displayName == "Texture Sample Volume " + id;
    case kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray:
        return node.parameter.displayName == "Texture Sample 2D Array " + id ||
            node.parameter.displayName == "Texture Sample Array " + id;
    default:
        return false;
    }
}

[[nodiscard]] bool ConstantNodeDisplayNameIsGenerated(const kb::render::RenderMaterialGraphNode& node) {
    switch (node.kind) {
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
        return node.parameter.displayName == "RGB" || node.parameter.displayName == "Vector" ||
            node.parameter.displayName == "RGB Node";
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
        return node.parameter.displayName == "RGBA" || node.parameter.displayName == "Color" ||
            node.parameter.displayName == "RGBA Node";
    default:
        return false;
    }
}

[[nodiscard]] std::string GraphNodeDisplayTitle(const kb::render::RenderMaterialGraphNode& node) {
    if (node.parameter.displayName.empty()) {
        return GraphNodeTitle(node.kind);
    }
    if (MaterialEditorPanelIsTexturePreviewNode(node.kind) && TextureNodeDisplayNameIsGenerated(node)) {
        return GraphNodeTitle(node.kind);
    }
    if (ConstantNodeDisplayNameIsGenerated(node)) {
        return GraphNodeTitle(node.kind);
    }
    return node.parameter.displayName;
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

void DrawColorCheckerboard(HDC dc, const RECT& rect, int cellSize);

void DrawTexturePreviewBlock(
    HDC dc,
    const RECT& nodeRect,
    const RECT& preview,
    const kb::render::RenderMaterialGraphNode& node,
    const kb::render::RenderMaterialAssetData* material,
    const EditorSceneContext& sceneContext) {
    const float scale = NodeUiScale(nodeRect, node.kind);
    const int radius = std::max(5, ScaleMetric(7, scale));
    FillRoundedRect(dc, preview, RGB(6, 8, 11), radius);
    const RECT imageFace{
        preview.left + ScaleMetric(1, scale),
        preview.top + ScaleMetric(1, scale),
        preview.right - ScaleMetric(1, scale),
        preview.bottom - ScaleMetric(1, scale),
    };
    DrawColorCheckerboard(dc, imageFace, std::max(6, ScaleMetric(10, scale)));
    const kb::assets::AssetMetadata* metadata = TextureNodeMetadata(material, node, sceneContext);
    if (metadata != nullptr) {
        if (const EditorTexturePreviewImage* image = EditorTexturePreviewService::PreviewFor(*metadata); image != nullptr) {
            EditorTexturePreviewService::DrawContain(dc, imageFace, *image, false);
        } else {
            GdiDrawing::FillRectAlpha(dc, imageFace, RGB(57, 73, 84), 36);
        }
    } else {
        GdiDrawing::FillRectAlpha(dc, imageFace, RGB(55, 66, 78), 24);
    }
    StrokeRoundedRect(dc, preview, metadata == nullptr ? MaterialGraphTheme::NodePanelBorder : RGB(82, 126, 136), radius);
}

void DrawTextureSamplePreview(HDC dc, const RECT& nodeRect, const kb::render::RenderMaterialGraphNode& node, const kb::render::RenderMaterialAssetData* material, const EditorSceneContext& sceneContext) {
    if (!MaterialEditorPanelIsTextureSamplePreviewNode(node.kind)) {
        return;
    }

    DrawTexturePreviewBlock(dc, nodeRect, MaterialEditorPanelTextureSamplePreviewRect(nodeRect), node, material, sceneContext);
}

void DrawTextureParameterValue(HDC dc, const RECT& nodeRect, const kb::render::RenderMaterialGraphNode& node, const kb::render::RenderMaterialAssetData* material, const EditorSceneContext& sceneContext) {
    if (!MaterialEditorPanelIsTextureObjectPreviewNode(node.kind)) {
        return;
    }

    const RECT valueRect = MaterialEditorPanelTextureParameterRect(nodeRect);
    DrawTexturePreviewBlock(dc, nodeRect, valueRect, node, material, sceneContext);
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

void DrawGraphNumericValueField(
    HDC dc,
    const RECT& fieldRect,
    const RECT& textRect,
    const char* text,
    bool editing,
    float scale) {
    const int radius = std::max(4, ScaleMetric(6, scale));
    FillRoundedRect(dc, fieldRect, MaterialGraphTheme::Field, radius);
    StrokeRoundedRect(dc, fieldRect, editing ? MaterialGraphTheme::FieldFocus : MaterialGraphTheme::FieldBorder, radius, editing ? 2 : 1);

    const RECT fillBounds{
        fieldRect.left + 1,
        fieldRect.top + 1,
        fieldRect.right - 1,
        fieldRect.bottom - 1,
    };
    GdiDrawing::FillRectAlpha(dc, RECT{ fillBounds.left + 2, fillBounds.top + 1, fillBounds.right - 2, fillBounds.top + std::max(2, ScaleMetric(3, scale)) }, RGB(255, 255, 255), 20);
    DrawGraphText(
        dc,
        textRect,
        text,
        RGB(238, 238, 238),
        ScaleMetric(kGraphPinFontSize, scale),
        FW_NORMAL,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawGraphPlainValueField(
    HDC dc,
    const RECT& fieldRect,
    const RECT& textRect,
    const char* text,
    bool editing,
    float scale) {
    const int radius = std::max(3, ScaleMetric(4, scale));
    FillRoundedRect(dc, fieldRect, RGB(13, 16, 22), radius);
    StrokeRoundedRect(dc, fieldRect, editing ? MaterialGraphTheme::FieldFocus : RGB(0, 0, 0), radius, editing ? 2 : 1);
    GdiDrawing::FillRectAlpha(dc, RECT{
        fieldRect.left + ScaleMetric(2, scale),
        fieldRect.top + ScaleMetric(1, scale),
        fieldRect.right - ScaleMetric(2, scale),
        fieldRect.top + std::max(2, ScaleMetric(3, scale)),
    }, RGB(255, 255, 255), 12);
    DrawGraphText(
        dc,
        textRect,
        text,
        RGB(232, 238, 248),
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
    const int radius = std::max(4, ScaleMetric(7, scale));
    DrawColorCheckerboard(dc, rect, std::max(3, ScaleMetric(5, scale)));
    const BYTE alpha = static_cast<BYTE>(ColorByte(value.numbers[3]));
    GdiDrawing::FillRectAlpha(dc, rect, ColorRef(value), alpha);
    StrokeRoundedRect(dc, rect, selected ? RGB(126, 177, 235) : RGB(14, 17, 21), radius, selected ? 2 : 1);
    GdiDrawing::FillRectAlpha(dc, RECT{ rect.left + 1, rect.top + 1, rect.right - 1, std::min(rect.bottom, rect.top + ScaleMetric(5, scale)) }, RGB(255, 255, 255), 28);
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
    for (std::size_t index = 0U; index < componentCount; ++index) {
        const RECT fieldRect = MaterialEditorPanelColorWatcherChannelRect(nodeRect, kind, index, componentCount);
        std::string text = std::string{ kLabels[index] } + " " + std::to_string(ColorByte(value.numbers[index]));
        if (editing) {
            text = std::string{ kLabels[index] } + " " + std::string{ editBuffer } + "|";
        }
        DrawGraphPlainValueField(
            dc,
            fieldRect,
            RECT{
                fieldRect.left + ScaleMetric(4, scale),
                fieldRect.top,
                fieldRect.right - ScaleMetric(4, scale),
                fieldRect.bottom,
            },
            text.c_str(),
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
    const RECT swatch = MaterialEditorPanelColorWatcherSwatchRect(nodeRect, kind);
    const int swatchRadius = std::max(3, ScaleMetric(3, scale));
    FillRoundedRect(dc, swatch, ColorRef(value), swatchRadius);
    StrokeRoundedRect(dc, swatch, RGB(0, 0, 0), swatchRadius);
    GdiDrawing::FillRectAlpha(dc, RECT{
        swatch.left + ScaleMetric(1, scale),
        swatch.top + ScaleMetric(1, scale),
        swatch.right - ScaleMetric(1, scale),
        std::min<LONG>(swatch.bottom, swatch.top + ScaleMetric(4, scale)),
    }, RGB(255, 255, 255), 18);
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
            MaterialGraphTheme::TextMuted,
            ScaleMetric(kGraphPinFontSize, scale),
            FW_NORMAL,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawGraphNumericValueField(
            dc,
            fieldRect,
            RECT{
                fieldRect.left + ScaleMetric(8, scale),
                fieldRect.top,
                fieldRect.right - ScaleMetric(8, scale),
                fieldRect.bottom,
            },
            texts[index].c_str(),
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
        DrawGraphNumericValueField(
            dc,
            valueRect,
            RECT{ valueRect.left + ScaleMetric(13, scale), valueRect.top, valueRect.right - ScaleMetric(10, scale), valueRect.bottom },
            valueText.c_str(),
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
    FillRoundedRect(dc, watcher, MaterialGraphTheme::NodePanel, std::max(5, ScaleMetric(8, scale)));
    GdiDrawing::FillRectAlpha(dc, RECT{ watcher.left + ScaleMetric(2, scale), watcher.top + ScaleMetric(2, scale), watcher.right - ScaleMetric(2, scale), watcher.top + ScaleMetric(10, scale) }, RGB(255, 255, 255), 12);
    StrokeRoundedRect(dc, watcher, MaterialGraphTheme::NodePanelBorder, std::max(5, ScaleMetric(8, scale)));

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
        MaterialGraphTheme::TextMuted,
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

void DrawGraphNodeDirect(
    HDC dc,
    const RECT& rect,
    const RECT& clip,
    const MaterialGraphCanvas& canvas,
    std::uint32_t canvasNodeIndex,
    const kb::render::RenderMaterialGraphDocument& graph,
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialGraphNode& node,
    bool selected,
    const kb::render::RenderMaterialAssetData* material,
    const EditorSceneContext& sceneContext) {
    static_cast<void>(graph);
    static_cast<void>(assetId);
    const float scale = NodeUiScale(rect, node.kind);
    const int headerHeight = ScaleMetric(kGraphNodeHeaderHeight, scale);
    const int cornerDiameter = ScaleMetric(kGraphNodeCornerDiameter, scale);
    const int pinRadius = ScaleMetric(kGraphNodePinRadius, scale);
    const COLORREF body = MaterialGraphTheme::NodeBody;
    const COLORREF bodyTop = MaterialGraphTheme::NodePanel;
    const COLORREF bodyBottom = MaterialGraphTheme::NodeBodyBottom;
    const COLORREF accent = GraphNodeAccentColor(node.kind);
    const COLORREF headerTop = ScaleColor(GraphNodeHeaderColor(node.kind), selected ? 1.34F : 1.0F);
    const COLORREF headerBottom = ScaleColor(GraphNodeHeaderColor(node.kind), selected ? 0.92F : 0.72F);
    const COLORREF border = selected ? MaterialGraphTheme::NodeOutlineSelected : MaterialGraphTheme::NodeOutline;

    const int savedNodeDc = SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    HeroIconGdiplusRuntime::EnsureStarted();
    {
        {
            Gdiplus::Graphics frameGraphics(dc);
            frameGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            frameGraphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            frameGraphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            frameGraphics.SetClip(Gdiplus::Rect(
                static_cast<int>(clip.left),
                static_cast<int>(clip.top),
                std::max(0, static_cast<int>(clip.right - clip.left)),
                std::max(0, static_cast<int>(clip.bottom - clip.top))));
            DrawGraphNodeFrame(dc, frameGraphics, rect, body, scale);
        }

        struct PinDrawCommand {
            POINT point{};
            COLORREF color = RGB(255, 255, 255);
            bool tinted = false;
        };
        std::vector<PinDrawCommand> pinDrawCommands;

        const RECT inner{ rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2 };
        const RECT bodyRect{ inner.left, rect.top + headerHeight, inner.right, inner.bottom };
        DrawVerticalGradientClippedToRound(dc, bodyRect, inner, bodyTop, bodyBottom, std::max(2, cornerDiameter - 2));
        DrawVerticalGradientClippedToRound(dc, RECT{ inner.left, inner.top, inner.right, rect.top + headerHeight }, inner, headerTop, headerBottom, std::max(2, cornerDiameter - 2));
        static_cast<void>(accent);
        StrokeRoundedRect(dc, rect, border, cornerDiameter, selected ? 2 : 1);

        std::string title = GraphNodeDisplayTitle(node);
        const std::string_view supportTag = kb::render::RenderMaterialGraphNodeSupportShortTag(node.kind);
        if (!supportTag.empty()) {
            title += "  [";
            title += supportTag;
            title += "]";
        }
        const RECT titleRect{
            rect.left + ScaleMetric(14, scale),
            rect.top + ScaleMetric(2, scale),
            rect.right - ScaleMetric(14, scale),
            rect.top + headerHeight,
        };
        DrawGraphText(dc, titleRect, title.c_str(), RGB(246, 248, 251), ScaleMetric(kGraphTitleFontSize, scale), FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        const std::vector<MaterialEditorGraphDiagnosticMarker> diagnosticMarkers = MarkersForNode(sceneContext, node.id);
        DrawGraphDiagnosticMarker(dc, rect, diagnosticMarkers, scale);

        DrawTextureSamplePreview(dc, rect, node, material, sceneContext);
        DrawTextureParameterValue(dc, rect, node, material, sceneContext);
        DrawConstantValue(dc, rect, node, sceneContext);
        DrawParameterColorValue(dc, rect, node, material);
        DrawColorRampWatcher(dc, rect, node);

        const std::vector<std::pair<std::string, std::string>>inputPins = GraphInputPins(node);
        const std::vector<std::pair<std::string, std::string>>outputPins = GraphOutputPins(node);
        const bool splitPinLabelLanes = !inputPins.empty() && !outputPins.empty() &&
            !MaterialEditorPanelIsTextureSamplePreviewNode(node.kind) &&
            !MaterialEditorPanelIsTextureObjectPreviewNode(node.kind) &&
            !MaterialEditorPanelIsConstantNode(node.kind) &&
            !MaterialEditorPanelNodeHasColorWatcher(node.kind);
        const int splitInputRight = rect.left + (RectWidth(rect) / 2) - ScaleMetric(8, scale);
        const int splitOutputLeft = rect.left + (RectWidth(rect) / 2) + ScaleMetric(8, scale);
        if (!inputPins.empty()) {
            for (std::size_t index = 0; index < inputPins.size(); ++index) {
                const POINT scaledPin = GraphCanvasPointToPoint(canvas.PinCenterWindow(
                    canvasNodeIndex,
                    static_cast<std::uint32_t>(index),
                    false));
                pinDrawCommands.push_back(PinDrawCommand{
                    .point = scaledPin,
                    .color = GraphInputPinColor(node, inputPins[index].first),
                    .tinted = false,
                });
                const bool textureSample = MaterialEditorPanelIsTextureSamplePreviewNode(node.kind);
                const RECT texturePreview = textureSample ? MaterialEditorPanelTextureSamplePreviewRect(rect) : RECT{};
                int inputLabelRight = textureSample
                    ? std::max(rect.left + ScaleMetric(54, scale), texturePreview.left - ScaleMetric(10, scale))
                    : rect.right - ScaleMetric(18, scale);
                if (splitPinLabelLanes) {
                    inputLabelRight = std::min(inputLabelRight, splitInputRight);
                }
                DrawGraphText(
                    dc,
                    RECT{
                        rect.left + pinRadius + ScaleMetric(6, scale),
                        scaledPin.y - ScaleMetric(11, scale),
                        inputLabelRight,
                        scaledPin.y + ScaleMetric(11, scale),
                    },
                    std::string{ inputPins[index].second }.c_str(),
                    MaterialGraphTheme::Text,
                    ScaleMetric(kGraphPinFontSize, scale),
                    FW_NORMAL,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
        for (std::size_t index = 0U; index < outputPins.size(); ++index) {
            const POINT output = GraphCanvasPointToPoint(canvas.PinCenterWindow(
                canvasNodeIndex,
                static_cast<std::uint32_t>(index),
                true));
            pinDrawCommands.push_back(PinDrawCommand{
                .point = output,
                .color = GraphOutputPinColor(node, outputPins[index].first),
                .tinted = GraphOutputPinTinted(node.kind, outputPins[index].first),
            });
            RECT outputLabelRect{
                rect.left + ScaleMetric(6, scale),
                output.y - ScaleMetric(11, scale),
                output.x - pinRadius - ScaleMetric(12, scale),
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
                if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector ||
                    node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor) {
                    outputLabelRect.left = std::max(outputLabelRect.left, valueRect.right + ScaleMetric(2, scale));
                    outputLabelRect.right = output.x - pinRadius - ScaleMetric(1, scale);
                } else {
                    outputLabelRect.left = std::max(outputLabelRect.left, valueRect.right + ScaleMetric(8, scale));
                }
            } else if (MaterialEditorPanelNodeHasColorWatcher(node.kind)) {
                const RECT watcher = MaterialEditorPanelColorWatcherRect(rect, node.kind);
                outputLabelRect.left = std::max(outputLabelRect.left, watcher.right + ScaleMetric(2, scale));
                outputLabelRect.right = output.x - pinRadius - ScaleMetric(1, scale);
            }
            if (splitPinLabelLanes) {
                outputLabelRect.left = std::max<LONG>(outputLabelRect.left, splitOutputLeft);
            }
            const std::string outputLabel = outputPins[index].second;
            const int minOutputLabelWidth = ScaleMetric(
                static_cast<int>(outputLabel.size() * 6U) + 8,
                scale);
            if (outputLabelRect.right > outputLabelRect.left + minOutputLabelWidth) {
                DrawGraphText(
                    dc,
                    outputLabelRect,
                    outputLabel.c_str(),
                    GraphOutputPinLabelColor(node.kind, outputPins[index].first),
                    ScaleMetric(kGraphPinFontSize, scale),
                    FW_NORMAL,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }

        if (!pinDrawCommands.empty()) {
            Gdiplus::Graphics pinGraphics(dc);
            pinGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            pinGraphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            pinGraphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            pinGraphics.SetClip(Gdiplus::Rect(
                static_cast<int>(clip.left),
                static_cast<int>(clip.top),
                std::max(0, static_cast<int>(clip.right - clip.left)),
                std::max(0, static_cast<int>(clip.bottom - clip.top))));
            for (const PinDrawCommand& command : pinDrawCommands) {
                DrawGraphPin(pinGraphics, command.point, command.color, scale, command.tinted);
            }
        }
    }
    RestoreDC(dc, savedNodeDc);
}

void DrawGraphNode(
    HDC dc,
    const RECT& rect,
    const RECT& clip,
    const MaterialGraphCanvas& canvas,
    std::uint32_t canvasNodeIndex,
    const kb::render::RenderMaterialGraphDocument& graph,
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialGraphNode& node,
    bool selected,
    const kb::render::RenderMaterialAssetData* material,
    const EditorSceneContext& sceneContext) {
    const float scale = NodeUiScale(rect, node.kind);
    const RECT paintBounds = GraphNodePaintBounds(rect, scale);
    const std::optional<RECT> clippedPaintBounds = IntersectRectOptional(paintBounds, clip);
    if (!clippedPaintBounds.has_value()) {
        return;
    }

    const int width = RectWidth(*clippedPaintBounds);
    const int height = RectHeight(*clippedPaintBounds);
    if (width <= 0 || height <= 0) {
        return;
    }

    if (RectContainsRect(clip, paintBounds)) {
        DrawGraphNodeDirect(dc, rect, clip, canvas, canvasNodeIndex, graph, assetId, node, selected, material, sceneContext);
        return;
    }

    HDC memoryDc = CreateCompatibleDC(dc);
    if (memoryDc == nullptr) {
        return;
    }
    HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
    if (bitmap == nullptr) {
        DeleteDC(memoryDc);
        return;
    }
    HGDIOBJ previousBitmap = SelectObject(memoryDc, bitmap);
    if (previousBitmap == nullptr) {
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        return;
    }

    BitBlt(memoryDc, 0, 0, width, height, dc, clippedPaintBounds->left, clippedPaintBounds->top, SRCCOPY);

    RECT localRect = rect;
    OffsetRect(&localRect, -clippedPaintBounds->left, -clippedPaintBounds->top);
    const RECT localClip{ 0, 0, width, height };
    MaterialGraphCanvas localCanvas = canvas;
    const MaterialGraphCanvasRect canvasViewport = canvas.Viewport();
    localCanvas.SetViewport(MaterialGraphCanvasRect{
        canvasViewport.x - static_cast<float>(clippedPaintBounds->left),
        canvasViewport.y - static_cast<float>(clippedPaintBounds->top),
        canvasViewport.width,
        canvasViewport.height,
    });
    DrawGraphNodeDirect(memoryDc, localRect, localClip, localCanvas, canvasNodeIndex, graph, assetId, node, selected, material, sceneContext);

    BitBlt(dc, clippedPaintBounds->left, clippedPaintBounds->top, width, height, memoryDc, 0, 0, SRCCOPY);

    SelectObject(memoryDc, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
}

void DrawPendingGraphConnection(HDC dc, const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, const EditorSceneContext& sceneContext, kb::assets::AssetId assetId) {
    if (!sceneContext.HasMaterialGraphPinConnection() || sceneContext.MaterialGraphPinConnectionAssetId() != assetId) {
        return;
    }
    const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(graph, sceneContext.MaterialGraphPinConnectionNodeId());
    if (node == nullptr) {
        return;
    }

    MaterialGraphCanvasDocumentBuildResult canvasResult =
        MaterialEditorPanelBuildInteractiveGraphCanvas(content, graph, sceneContext, assetId);
    const std::optional<MaterialGraphCanvasPoint> anchorPoint = GraphCanvasPinWindowPoint(
        canvasResult.canvas,
        node->id,
        sceneContext.MaterialGraphPinConnectionPin(),
        sceneContext.MaterialGraphPinConnectionIsOutput());
    if (!anchorPoint.has_value()) {
        return;
    }

    const POINT anchor = GraphCanvasPointToPoint(*anchorPoint);
    const POINT cursor{ sceneContext.MaterialGraphPinConnectionX(), sceneContext.MaterialGraphPinConnectionY() };
    const COLORREF pendingColor = sceneContext.MaterialGraphPinConnectionIsOutput()
        ? GraphOutputPinColor(*node, sceneContext.MaterialGraphPinConnectionPin())
        : GraphInputPinColor(*node, sceneContext.MaterialGraphPinConnectionPin());
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics pendingLinkGraphics(dc);
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    pendingLinkGraphics.SetClip(Gdiplus::Rect(
        static_cast<int>(layout.graphCanvas.left),
        static_cast<int>(layout.graphCanvas.top),
        std::max(0, static_cast<int>(layout.graphCanvas.right - layout.graphCanvas.left)),
        std::max(0, static_cast<int>(layout.graphCanvas.bottom - layout.graphCanvas.top))));
    pendingLinkGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    pendingLinkGraphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    DrawGraphBezier(
        pendingLinkGraphics,
        sceneContext.MaterialGraphPinConnectionIsOutput() ? anchor : cursor,
        sceneContext.MaterialGraphPinConnectionIsOutput() ? cursor : anchor,
        pendingColor,
        std::clamp(ScaleMetric(3, sceneContext.MaterialGraphZoom()), 1, 3),
        GraphCanvasBezierHandleDistance(anchor, cursor, canvasResult.canvas.Zoom()));
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

constexpr int kMaterialGraphTexturePickerWidth = 720;
constexpr int kMaterialGraphTexturePickerHeight = 560;
constexpr int kMaterialGraphTexturePickerHeaderHeight = 34;
constexpr int kMaterialGraphTexturePickerMinTileWidth = 116;
constexpr int kMaterialGraphTexturePickerTileHeight = 150;
constexpr int kMaterialGraphTexturePickerTileGap = 10;
constexpr int kMaterialGraphTexturePickerMaxColumns = 4;

struct MaterialGraphTexturePickerRow {
    kb::assets::AssetId assetId{};
    std::string name;
    std::string path;
    bool clear = false;
};

[[nodiscard]] std::string MaterialGraphTexturePickerDisplayName(const kb::assets::AssetMetadata& metadata) {
    if (!metadata.name.empty()) {
        return metadata.name;
    }
    if (!metadata.virtualPath.empty()) {
        return metadata.virtualPath.stem().string();
    }
    return "Texture";
}

[[nodiscard]] std::string MaterialGraphTexturePickerDisplayPath(const kb::assets::AssetMetadata& metadata) {
    if (!metadata.virtualPath.empty()) {
        return kb::assets::NormalizeAssetPath(metadata.virtualPath);
    }
    return metadata.physicalPath.string();
}

[[nodiscard]] std::string MaterialGraphTexturePickerLower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

[[nodiscard]] bool MaterialGraphTexturePickerMatchesQuery(
    const MaterialGraphTexturePickerRow& row,
    std::string_view query) {
    if (query.empty()) {
        return true;
    }
    const std::string needle = MaterialGraphTexturePickerLower(std::string{ query });
    const std::string haystack = MaterialGraphTexturePickerLower(row.name + " " + row.path);
    return haystack.find(needle) != std::string::npos;
}

[[nodiscard]] EditorTextureAssetPickerFilter MaterialGraphTexturePickerFilterForNodeKind(
    kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleCube:
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectCube:
        return EditorTextureAssetPickerFilter::TextureCube;
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume:
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume:
        return EditorTextureAssetPickerFilter::TextureVolume;
    case kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray:
    case kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray:
        return EditorTextureAssetPickerFilter::Texture2DArray;
    default:
        return EditorTextureAssetPickerFilter::Texture2D;
    }
}

[[nodiscard]] EditorTextureAssetPickerFilter MaterialGraphTexturePickerFilterForOpenNode(
    const EditorSceneContext& sceneContext) {
    const std::optional<kb::render::RenderMaterialAssetData>& working = sceneContext.MaterialEditor().WorkingCopy();
    if (!working.has_value()) {
        return EditorTextureAssetPickerFilter::Texture2D;
    }
    const kb::render::RenderMaterialGraphNode* node =
        kb::render::FindRenderMaterialGraphNode(working->graph, sceneContext.MaterialGraphTexturePickerNodeId());
    return node == nullptr
        ? EditorTextureAssetPickerFilter::Texture2D
        : MaterialGraphTexturePickerFilterForNodeKind(node->kind);
}

[[nodiscard]] std::vector<MaterialGraphTexturePickerRow> MaterialGraphTexturePickerRows(
    const EditorSceneContext& sceneContext) {
    const EditorTextureAssetPickerFilter filter = MaterialGraphTexturePickerFilterForOpenNode(sceneContext);
    std::vector<MaterialGraphTexturePickerRow> rows;
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (!EditorTextureAssetMatchesFilter(metadata, filter)) {
            continue;
        }
        MaterialGraphTexturePickerRow row{
            .assetId = metadata.id,
            .name = MaterialGraphTexturePickerDisplayName(metadata),
            .path = MaterialGraphTexturePickerDisplayPath(metadata),
        };
        if (!MaterialGraphTexturePickerMatchesQuery(row, sceneContext.MaterialGraphTexturePickerSearchQuery())) {
            continue;
        }
        rows.push_back(std::move(row));
    }
    std::ranges::sort(rows, [](const MaterialGraphTexturePickerRow& lhs, const MaterialGraphTexturePickerRow& rhs) {
        if (lhs.name != rhs.name) {
            return lhs.name < rhs.name;
        }
        return lhs.assetId.value < rhs.assetId.value;
    });
    rows.insert(rows.begin(), MaterialGraphTexturePickerRow{
        .name = "None / Clear",
        .path = "Remove the texture assignment",
        .clear = true,
    });
    return rows;
}

[[nodiscard]] RECT MaterialGraphTexturePickerRect(const RECT& content) noexcept {
    const RECT host{
        content.left + 12,
        content.top + kHeaderHeight + 12,
        content.right - 12,
        content.bottom - 12,
    };
    const int hostWidth = std::max(0, RectWidth(host));
    const int hostHeight = std::max(0, RectHeight(host));
    const int width = std::min(kMaterialGraphTexturePickerWidth, hostWidth);
    const int height = std::min(kMaterialGraphTexturePickerHeight, hostHeight);
    const int left = std::clamp(
        host.left + (hostWidth - width) / 2,
        host.left,
        std::max(host.left, host.right - width));
    const int top = std::clamp(
        host.top + (hostHeight - height) / 2,
        host.top,
        std::max(host.top, host.bottom - height));
    return RECT{ left, top, left + width, top + height };
}

[[nodiscard]] RECT MaterialGraphTexturePickerSearchRect(const RECT& picker) noexcept {
    return RECT{
        picker.left + 10,
        picker.top + 6,
        picker.right - 184,
        picker.top + 34,
    };
}

[[nodiscard]] RECT MaterialGraphTexturePickerAcceptRect(const RECT& picker) noexcept {
    return RECT{
        picker.right - 174,
        picker.top + 6,
        picker.right - 92,
        picker.top + 34,
    };
}

[[nodiscard]] RECT MaterialGraphTexturePickerCancelRect(const RECT& picker) noexcept {
    return RECT{
        picker.right - 82,
        picker.top + 6,
        picker.right - 10,
        picker.top + 34,
    };
}

[[nodiscard]] RECT MaterialGraphTexturePickerViewportRect(const RECT& picker) noexcept {
    return RECT{
        picker.left + 10,
        picker.top + kMaterialGraphTexturePickerHeaderHeight + 10,
        picker.right - 10,
        picker.bottom - 10,
    };
}

[[nodiscard]] int MaterialGraphTexturePickerContentHeight(std::size_t rowCount) noexcept {
    if (rowCount == 0U) {
        return 0;
    }
    return static_cast<int>(rowCount) * kMaterialGraphTexturePickerTileHeight +
        static_cast<int>(rowCount - 1U) * kMaterialGraphTexturePickerTileGap;
}

[[nodiscard]] int MaterialGraphTexturePickerMaxScrollImpl(
    const RECT& content,
    const EditorSceneContext& sceneContext) {
    if (!sceneContext.IsMaterialGraphTexturePickerOpen()) {
        return 0;
    }
    const std::vector<MaterialGraphTexturePickerRow> rows = MaterialGraphTexturePickerRows(sceneContext);
    return MaterialEditorPanelRenderer::ResolveGraphTexturePickerLayout(content, rows.size()).maxScroll;
}

[[nodiscard]] MaterialEditorGraphTexturePickerHit MaterialGraphTexturePickerHitImpl(
    const RECT& content,
    const EditorSceneContext& sceneContext,
    int x,
    int y) {
    if (!sceneContext.IsMaterialGraphTexturePickerOpen()) {
        return {};
    }
    const std::vector<MaterialGraphTexturePickerRow> rows = MaterialGraphTexturePickerRows(sceneContext);
    const MaterialEditorGraphTexturePickerLayout layout =
        MaterialEditorPanelRenderer::ResolveGraphTexturePickerLayout(
            content,
            rows.size(),
            sceneContext.MaterialGraphTexturePickerScrollOffset());
    const RECT picker = layout.picker;
    if (!MaterialEditorPanelPointInRect(picker, x, y)) {
        return MaterialEditorGraphTexturePickerHit{ .kind = MaterialEditorGraphTexturePickerHitKind::Backdrop };
    }
    if (MaterialEditorPanelPointInRect(layout.search, x, y)) {
        return MaterialEditorGraphTexturePickerHit{ .kind = MaterialEditorGraphTexturePickerHitKind::Search };
    }
    if (MaterialEditorPanelPointInRect(layout.accept, x, y)) {
        return MaterialEditorGraphTexturePickerHit{ .kind = MaterialEditorGraphTexturePickerHitKind::Accept };
    }
    if (MaterialEditorPanelPointInRect(layout.cancel, x, y)) {
        return MaterialEditorGraphTexturePickerHit{ .kind = MaterialEditorGraphTexturePickerHitKind::Cancel };
    }

    const RECT viewport = layout.viewport;
    if (!MaterialEditorPanelPointInRect(viewport, x, y)) {
        return {};
    }
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        const RECT tile = layout.itemRects[index];
        if (MaterialEditorPanelPointInRect(tile, x, y)) {
            return MaterialEditorGraphTexturePickerHit{
                .kind = rows[index].clear
                    ? MaterialEditorGraphTexturePickerHitKind::Clear
                    : MaterialEditorGraphTexturePickerHitKind::Texture,
                .assetId = rows[index].assetId,
            };
        }
    }
    return {};
}

void DrawMaterialGraphTexturePickerButton(HDC dc, const RECT& rect, const char* label, bool enabled) {
    const COLORREF fill = enabled ? RGB(38, 43, 51) : RGB(30, 33, 38);
    const COLORREF border = enabled ? RGB(83, 128, 165) : RGB(59, 65, 75);
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    DrawText(
        dc,
        RECT{ rect.left + 6, rect.top, rect.right - 6, rect.bottom },
        label,
        enabled ? RGB(236, 242, 249) : RGB(126, 135, 149),
        10,
        FW_SEMIBOLD,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawMaterialGraphTexturePickerTile(
    HDC dc,
    const RECT& tile,
    const MaterialGraphTexturePickerRow& row,
    bool selected,
    const EditorSceneContext& sceneContext) {
    FillRoundedRect(dc, tile, selected ? RGB(38, 73, 88) : RGB(19, 20, 24), 4);
    StrokeRoundedRect(dc, tile, selected ? RGB(92, 176, 207) : RGB(0, 0, 0), 4, selected ? 2 : 1);

    const RECT image{
        tile.left + 6,
        tile.top + 6,
        tile.right - 6,
        tile.top + 110,
    };
    GdiDrawing::DrawSharpFrame(dc, image, RGB(0, 0, 0), RGB(7, 8, 10));
    DrawColorCheckerboard(dc, RECT{ image.left + 1, image.top + 1, image.right - 1, image.bottom - 1 }, 10);
    if (const kb::assets::AssetMetadata* metadata = row.clear
            ? nullptr
            : sceneContext.Scene().Assets().Manager().Registry().Find(row.assetId);
        metadata != nullptr) {
        if (const EditorTexturePreviewImage* preview = EditorTexturePreviewService::PreviewFor(*metadata); preview != nullptr) {
            EditorTexturePreviewService::DrawContain(dc, RECT{ image.left + 1, image.top + 1, image.right - 1, image.bottom - 1 }, *preview, false);
        }
    }

    DrawText(
        dc,
        RECT{ tile.left + 8, image.bottom + 8, tile.right - 8, tile.bottom - 6 },
        row.name.c_str(),
        RGB(232, 238, 246),
        10,
        FW_SEMIBOLD,
        DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (row.clear) {
        DrawText(
            dc,
            image,
            "Clear",
            RGB(196, 207, 220),
            12,
            FW_SEMIBOLD,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawMaterialGraphTexturePickerOverlay(
    HDC dc,
    const RECT& content,
    const EditorSceneContext& sceneContext) {
    if (!sceneContext.IsMaterialGraphTexturePickerOpen()) {
        return;
    }

    const RECT body{ content.left, content.top + kHeaderHeight, content.right, content.bottom };
    GdiDrawing::FillRectAlpha(dc, body, RGB(0, 0, 0), 72);

    const std::vector<MaterialGraphTexturePickerRow> rows = MaterialGraphTexturePickerRows(sceneContext);
    const MaterialEditorGraphTexturePickerLayout layout =
        MaterialEditorPanelRenderer::ResolveGraphTexturePickerLayout(
            content,
            rows.size(),
            sceneContext.MaterialGraphTexturePickerScrollOffset());
    const RECT picker = layout.picker;
    GdiDrawing::DrawSharpFrame(dc, picker, RGB(28, 31, 36), RGB(47, 52, 61));

    const RECT search = layout.search;
    FillRoundedRect(dc, search, RGB(17, 20, 26), 4);
    StrokeRoundedRect(dc, search, RGB(83, 128, 165), 4, 1);
    std::string searchText{ sceneContext.MaterialGraphTexturePickerSearchQuery() };
    searchText += "|";
    DrawText(
        dc,
        RECT{ search.left + 10, search.top, search.right - 10, search.bottom },
        searchText.c_str(),
        RGB(236, 242, 249),
        10,
        FW_NORMAL);

    DrawMaterialGraphTexturePickerButton(dc, layout.accept, "Accept", layout.acceptEnabled);
    DrawMaterialGraphTexturePickerButton(dc, layout.cancel, "Cancel", true);

    const RECT viewport = layout.viewport;
    const int maxScroll = layout.maxScroll;
    const int scrollOffset = layout.scrollOffset;
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        const RECT tile = layout.itemRects[index];
        if (tile.bottom < viewport.top || tile.top > viewport.bottom) {
            continue;
        }
        DrawMaterialGraphTexturePickerTile(
            dc,
            tile,
            rows[index],
            rows[index].clear
                ? !sceneContext.MaterialGraphTexturePickerSelectedAssetId().IsValid()
                : rows[index].assetId == sceneContext.MaterialGraphTexturePickerSelectedAssetId(),
            sceneContext);
    }
    if (rows.empty()) {
        DrawText(
            dc,
            viewport,
            "No textures",
            RGB(126, 135, 149),
            11,
            FW_NORMAL,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    RestoreDC(dc, savedDc);

    if (maxScroll > 0) {
        const int trackLeft = picker.right - 7;
        GdiDrawing::FillRectColor(dc, RECT{ trackLeft, viewport.top, trackLeft + 3, viewport.bottom }, RGB(36, 41, 49));
        const int viewportHeight = std::max(1, RectHeight(viewport));
        const std::size_t rowCount = rows.empty()
            ? 0U
            : ((rows.size() + static_cast<std::size_t>(layout.columns) - 1U) /
                  static_cast<std::size_t>(layout.columns));
        const int contentHeight = std::max(1, MaterialGraphTexturePickerContentHeight(rowCount));
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
    MaterialGraphCanvasDocumentBuildResult canvasResult =
        MaterialEditorPanelBuildInteractiveGraphCanvas(content, graphView, sceneContext, assetId);

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
    {
        Gdiplus::Graphics linkGraphics(dc);
        linkGraphics.SetClip(Gdiplus::Rect(
            static_cast<int>(layout.graphCanvas.left),
            static_cast<int>(layout.graphCanvas.top),
            std::max(0, static_cast<int>(layout.graphCanvas.right - layout.graphCanvas.left)),
            std::max(0, static_cast<int>(layout.graphCanvas.bottom - layout.graphCanvas.top))));
        linkGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        linkGraphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        const std::vector<MaterialGraphCanvasNode>& canvasNodes = canvasResult.canvas.Nodes();
        for (const MaterialGraphCanvasLink& link : canvasResult.canvas.Links()) {
            if (link.fromNode >= canvasNodes.size() || link.toNode >= canvasNodes.size()) {
                continue;
            }
            const MaterialGraphCanvasNode& fromCanvasNode = canvasNodes[link.fromNode];
            if (link.fromPin >= fromCanvasNode.outputs.size()) {
                continue;
            }
            const std::uint32_t fromNodeId = static_cast<std::uint32_t>(std::strtoul(fromCanvasNode.stableId.c_str(), nullptr, 10));
            const kb::render::RenderMaterialGraphNode* fromNode = kb::render::FindRenderMaterialGraphNode(graphView, fromNodeId);
            const POINT from = GraphCanvasPointToPoint(canvasResult.canvas.PinCenterWindow(link.fromNode, link.fromPin, true));
            const POINT to = GraphCanvasPointToPoint(canvasResult.canvas.PinCenterWindow(link.toNode, link.toPin, false));
            const std::string& fromPin = fromCanvasNode.outputs[link.fromPin].stableId;
            const COLORREF linkColor = fromNode == nullptr
                ? MaterialGraphTheme::LinkFallback
                : GraphOutputPinColor(*fromNode, fromPin);
            DrawGraphBezier(
                linkGraphics,
                from,
                to,
                linkColor,
                std::clamp(ScaleMetric(3, canvasResult.canvas.Zoom()), 1, 3),
                GraphCanvasBezierHandleDistance(from, to, canvasResult.canvas.Zoom()));
            }
    }
    for (const kb::render::RenderMaterialGraphNode& node : graphView.nodes) {
        if (GraphNodeHiddenByCollapsedComposite(graphView, node.id)) {
            continue;
        }
        const std::optional<RECT> nodeRect = MaterialEditorPanelRenderer::GraphNodeRect(content, graphView, node.id, sceneContext, assetId);
        const auto canvasNode = std::ranges::find_if(
            canvasResult.canvas.Nodes(),
            [&node](const MaterialGraphCanvasNode& candidate) { return candidate.stableId == std::to_string(node.id); });
        if (nodeRect.has_value() && canvasNode != canvasResult.canvas.Nodes().end()) {
            const std::uint32_t canvasNodeIndex = static_cast<std::uint32_t>(
                std::distance(canvasResult.canvas.Nodes().begin(), canvasNode));
            DrawGraphNode(
                dc,
                *nodeRect,
                layout.graphCanvas,
                canvasResult.canvas,
                canvasNodeIndex,
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
    return metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance" ||
        metadata.type == kb::render::kRenderMaterialGraphAssetType;
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

void DrawDetailsPanel(
    HDC dc,
    const MaterialEditorPanelLayout& layout,
    const MaterialEditorPanelDetailsRows& rows,
    const MaterialEditorDetailsLayout& detailsLayout) {
    if (!detailsLayout.visible) {
        return;
    }

    DrawGraphOverlay(dc, layout.detailsPanel, RGB(22, 25, 29), RGB(54, 61, 71));
    DrawText(dc, RECT{ layout.detailsPanel.left + 10, layout.detailsPanel.top + 8, layout.detailsPanel.right - 10, layout.detailsPanel.top + 30 }, rows.title.c_str(), RGB(235, 238, 243), 12, FW_SEMIBOLD);
    FillRoundedRect(dc, detailsLayout.searchRect, rows.findFocused ? RGB(34, 45, 55) : RGB(30, 34, 40), 4);
    StrokeRoundedRect(dc, detailsLayout.searchRect, rows.findFocused ? RGB(83, 128, 165) : RGB(62, 70, 82), 4);
    const std::string searchText = rows.findQuery.empty() ? "Find in material" : rows.findQuery;
    DrawText(dc, RECT{ detailsLayout.searchRect.left + 8, detailsLayout.searchRect.top, detailsLayout.searchRect.right - 8, detailsLayout.searchRect.bottom }, searchText.c_str(), rows.findQuery.empty() ? RGB(151, 162, 176) : RGB(232, 237, 243), 9, rows.findFocused ? FW_SEMIBOLD : FW_NORMAL, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    const auto sectionLabel = [](MaterialEditorDetailsSection section) noexcept {
        switch (section) {
        case MaterialEditorDetailsSection::ParentChain: return "Parent Chain";
        case MaterialEditorDetailsSection::InstanceOverrides: return "Instance Overrides";
        case MaterialEditorDetailsSection::StaticSwitches: return "Static Switches";
        case MaterialEditorDetailsSection::LayerStack: return "Layer Stack";
        case MaterialEditorDetailsSection::FindResults: return "Find";
        case MaterialEditorDetailsSection::NodeProperties: return "Node Properties";
        case MaterialEditorDetailsSection::MaterialDiff: return "Material Diff";
        case MaterialEditorDetailsSection::DebugChannels: return "Debug Channels";
        case MaterialEditorDetailsSection::Parameters: return "Parameters";
        case MaterialEditorDetailsSection::TextureSlots: return "Texture Slots";
        case MaterialEditorDetailsSection::MaterialStats: return "Material Stats";
        case MaterialEditorDetailsSection::ShaderViewer: return "Shader Viewer";
        }
        return "Details";
    };

    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, detailsLayout.viewport.left, detailsLayout.viewport.top, detailsLayout.viewport.right, detailsLayout.viewport.bottom);
    for (const MaterialEditorDetailsLayoutItem& item : detailsLayout.items) {
        if (RectWidth(item.clippedRect) <= 0 || RectHeight(item.clippedRect) <= 0) {
            continue;
        }
        if (item.kind == MaterialEditorDetailsItemKind::SectionHeader) {
            DrawText(dc, item.rect, sectionLabel(item.section), RGB(157, 198, 241), 10, FW_SEMIBOLD);
            continue;
        }

        std::string text;
        COLORREF color = RGB(198, 205, 218);
        int weight = FW_NORMAL;
        switch (item.kind) {
        case MaterialEditorDetailsItemKind::ParentRow: {
            const MaterialEditorInstanceParentChainRow& row = rows.instanceParentRows[item.index];
            text = (row.current ? "* " : "  ") + row.label;
            color = row.current ? RGB(234, 225, 255) : RGB(201, 193, 222);
            weight = row.current ? FW_SEMIBOLD : FW_NORMAL;
            break;
        }
        case MaterialEditorDetailsItemKind::InstanceOverrideGroupRow: {
            const MaterialEditorInstanceOverrideGroupRow& row = rows.instanceOverrideGroupRows[item.index];
            text = std::string{ row.expanded ? "v " : "> " } + MaterialEditorPanelParameterGroupName(row.group) + "  " + std::to_string(row.activeOverrideCount) + "/" + std::to_string(row.totalParameterCount) + " overrides";
            break;
        }
        case MaterialEditorDetailsItemKind::StaticSwitchRow: {
            const MaterialEditorInstanceStaticSwitchRow& row = rows.instanceStaticSwitchRows[item.index];
            text = row.displayName + " = " + row.value + (row.overrideActive ? " override" : " parent");
            break;
        }
        case MaterialEditorDetailsItemKind::LayerRow: {
            const MaterialEditorLayerTreeRow& row = rows.layerTreeRows[item.index];
            text = std::string{ row.enabled ? "[on] " : "[off] " } + (row.layerName.empty() ? ("Layer " + std::to_string(row.index + 1U)) : row.layerName);
            break;
        }
        case MaterialEditorDetailsItemKind::FindResultRow:
            text = rows.findResults[item.index].label + "  " + rows.findResults[item.index].detail;
            break;
        case MaterialEditorDetailsItemKind::NodePropertyRow: {
            const MaterialEditorGraphNodeProperty& row = rows.nodePropertyRows[item.index];
            text = row.displayName + " = " + MaterialEditorPanelParameterValueText(row.value);
            if (row.kind == MaterialEditorGraphNodePropertyKind::TextureAsset) {
                text += "  Pick";
            }
            break;
        }
        case MaterialEditorDetailsItemKind::NodePropertyOptionRow: {
            const MaterialEditorGraphNodeProperty& property = rows.nodePropertyRows[item.index];
            const MaterialEditorGraphNodePropertyOption& option = property.options[item.optionIndex];
            const bool active = option.value == property.value.text;
            FillRoundedRect(dc, item.rect, active ? RGB(48, 83, 109) : RGB(27, 31, 37), 4);
            text = option.label;
            color = active ? RGB(239, 246, 252) : RGB(202, 211, 222);
            weight = active ? FW_SEMIBOLD : FW_NORMAL;
            break;
        }
        case MaterialEditorDetailsItemKind::MaterialDiffRow:
            text = rows.materialDiffRows[item.index];
            color = RGB(235, 215, 190);
            break;
        case MaterialEditorDetailsItemKind::DebugChannelRow:
            text = rows.debugChannelRows[item.index].label + "  " + rows.debugChannelRows[item.index].value;
            break;
        case MaterialEditorDetailsItemKind::ParameterRow:
            text = rows.parameterRows[item.index];
            break;
        case MaterialEditorDetailsItemKind::TextureParameterRow:
            text = rows.textureSlotRows[item.index];
            break;
        case MaterialEditorDetailsItemKind::MaterialStatsPassRow: {
            const MaterialEditorMaterialStatsPassRow& row = rows.materialStats.passRows[item.index];
            text = row.passName + (row.graphProgram ? " graph" : " builtin") + " inst " + std::to_string(row.instructionEstimate) + " tex " + std::to_string(row.textureSampleCount) + "/" + std::to_string(row.samplerCount);
            break;
        }
        case MaterialEditorDetailsItemKind::MaterialStatsWarningRow:
            text = "! " + rows.materialStats.warnings[item.index];
            color = RGB(244, 187, 121);
            break;
        case MaterialEditorDetailsItemKind::ShaderSourceRow: {
            const MaterialEditorShaderSourceView& row = rows.shaderViewer.sources[item.index];
            text = row.passName + " " + row.stageName + " " + row.backendName;
            break;
        }
        case MaterialEditorDetailsItemKind::ShaderReflectionRow: {
            const MaterialEditorShaderReflectionRow& row = rows.shaderViewer.reflectionRows[item.index];
            text = row.category + " " + row.name + (row.stableId.empty() ? "" : (" [" + row.stableId + "]"));
            break;
        }
        case MaterialEditorDetailsItemKind::ShaderWarningRow:
            text = "! " + rows.shaderViewer.warnings[item.index];
            color = RGB(244, 187, 121);
            break;
        case MaterialEditorDetailsItemKind::SectionHeader:
            break;
        }
        DrawText(dc, item.rect, text.c_str(), color, 9, weight, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
    RestoreDC(dc, savedDc);

    if (detailsLayout.maxScroll > 0) {
        const int trackLeft = layout.detailsPanel.right - 7;
        const int viewportHeight = std::max(1, RectHeight(detailsLayout.viewport));
        const int thumbHeight = std::max(24, viewportHeight * viewportHeight / std::max(1, detailsLayout.contentHeight));
        const int thumbTop = detailsLayout.viewport.top + (viewportHeight - thumbHeight) * detailsLayout.scrollOffset / std::max(1, detailsLayout.maxScroll);
        GdiDrawing::FillRectColor(dc, RECT{ trackLeft, detailsLayout.viewport.top, trackLeft + 3, detailsLayout.viewport.bottom }, RGB(36, 41, 49));
        GdiDrawing::FillRectColor(dc, RECT{ trackLeft - 1, thumbTop, trackLeft + 4, thumbTop + thumbHeight }, RGB(96, 111, 130));
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
    if (sceneContext.MaterialEditor().InfoPanelVisible() && document->material.has_value()) {
        const MaterialEditorPanelDetailsRows details = MaterialEditorPanelRenderer::DetailsRowsForDocument(
            sceneContext,
            *document->material,
            metadata.type == "RenderMaterialInstance");
        const MaterialEditorDetailsLayout detailsLayout = MaterialEditorPanelRenderer::ResolveDetailsLayout(
            content,
            details,
            sceneContext.MaterialEditorDetailsScrollOffset());
        DrawDetailsPanel(dc, layout, details, detailsLayout);
    }
    DrawMaterialGraphTexturePickerOverlay(dc, content, sceneContext);
}

} // namespace

MaterialEditorOpaqueOverlayHit MaterialEditorPanelRenderer::OpaqueOverlayAt(
    const RECT& content,
    const EditorSceneContext& sceneContext,
    int x,
    int y) {
    if (sceneContext.IsMaterialGraphContextMenuOpen()) {
        const RECT menu = GraphContextMenuRect(sceneContext);
        if (MaterialEditorPanelPointInRect(menu, x, y)) {
            return MaterialEditorOpaqueOverlayHit{
                .kind = MaterialEditorOpaqueOverlayKind::ContextMenu,
                .rect = menu,
            };
        }
    }

    const MaterialEditorPanelLayout layout = ResolveLayout(content);
    if (sceneContext.MaterialEditor().InfoPanelVisible() &&
        MaterialEditorPanelRectWidth(layout.detailsPanel) >= 220 &&
        MaterialEditorPanelRectHeight(layout.detailsPanel) >= 140 &&
        MaterialEditorPanelPointInRect(layout.detailsPanel, x, y)) {
        return MaterialEditorOpaqueOverlayHit{
            .kind = MaterialEditorOpaqueOverlayKind::Details,
            .rect = layout.detailsPanel,
        };
    }
    if (!sceneContext.MaterialEditor().Diagnostics().empty() &&
        MaterialEditorPanelRectWidth(layout.diagnosticsPanel) >= 160 &&
        MaterialEditorPanelRectHeight(layout.diagnosticsPanel) >= 72 &&
        MaterialEditorPanelPointInRect(layout.diagnosticsPanel, x, y)) {
        return MaterialEditorOpaqueOverlayHit{
            .kind = MaterialEditorOpaqueOverlayKind::Diagnostics,
            .rect = layout.diagnosticsPanel,
        };
    }
    if (sceneContext.MaterialEditor().OpenAssetId().IsValid() &&
        MaterialEditorPanelPointInRect(layout.previewFrame, x, y)) {
        return MaterialEditorOpaqueOverlayHit{
            .kind = MaterialEditorOpaqueOverlayKind::Preview,
            .rect = layout.previewFrame,
        };
    }
    return {};
}

MaterialEditorGraphTexturePickerLayout MaterialEditorPanelRenderer::ResolveGraphTexturePickerLayout(
    const RECT& content,
    std::size_t itemCount,
    int requestedScrollOffset) {
    MaterialEditorGraphTexturePickerLayout layout;
    layout.picker = MaterialGraphTexturePickerRect(content);
    layout.search = MaterialGraphTexturePickerSearchRect(layout.picker);
    layout.accept = MaterialGraphTexturePickerAcceptRect(layout.picker);
    layout.cancel = MaterialGraphTexturePickerCancelRect(layout.picker);
    layout.viewport = MaterialGraphTexturePickerViewportRect(layout.picker);
    const int viewportWidth = std::max(1, RectWidth(layout.viewport));
    layout.columns = std::clamp(
        (viewportWidth + kMaterialGraphTexturePickerTileGap) /
            (kMaterialGraphTexturePickerMinTileWidth + kMaterialGraphTexturePickerTileGap),
        1,
        kMaterialGraphTexturePickerMaxColumns);
    layout.tileWidth = std::max(
        1,
        (viewportWidth - ((layout.columns - 1) * kMaterialGraphTexturePickerTileGap)) / layout.columns);
    const std::size_t rowCount = itemCount == 0U
        ? 0U
        : ((itemCount + static_cast<std::size_t>(layout.columns) - 1U) /
              static_cast<std::size_t>(layout.columns));
    layout.maxScroll = std::max(
        0,
        MaterialGraphTexturePickerContentHeight(rowCount) - RectHeight(layout.viewport));
    layout.scrollOffset = std::clamp(requestedScrollOffset, 0, layout.maxScroll);
    layout.itemRects.reserve(itemCount);
    for (std::size_t index = 0U; index < itemCount; ++index) {
        const int column = static_cast<int>(index % static_cast<std::size_t>(layout.columns));
        const int row = static_cast<int>(index / static_cast<std::size_t>(layout.columns));
        const int x = layout.viewport.left + column * (layout.tileWidth + kMaterialGraphTexturePickerTileGap);
        const int y = layout.viewport.top + row * (kMaterialGraphTexturePickerTileHeight + kMaterialGraphTexturePickerTileGap) - layout.scrollOffset;
        layout.itemRects.push_back(RECT{ x, y, x + layout.tileWidth, y + kMaterialGraphTexturePickerTileHeight });
    }
    return layout;
}

int MaterialEditorPanelRenderer::GraphTexturePickerMaxScroll(
    const RECT& content,
    const EditorSceneContext& sceneContext) {
    return MaterialGraphTexturePickerMaxScrollImpl(content, sceneContext);
}

MaterialEditorGraphTexturePickerHit MaterialEditorPanelRenderer::GraphTexturePickerHit(
    const RECT& content,
    const EditorSceneContext& sceneContext,
    int x,
    int y) {
    return MaterialGraphTexturePickerHitImpl(content, sceneContext, x, y);
}

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
