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
#include <optional>
#include <sstream>
#include <string>
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
constexpr int kGraphNodeCornerDiameter = 10;
constexpr int kGraphTitleFontSize = 11;
constexpr int kGraphPinFontSize = 10;
constexpr int kGraphMinTextPointSize = 9;

namespace BlenderGraphTheme {
constexpr COLORREF Canvas = RGB(32, 32, 32);
constexpr COLORREF GridDot = RGB(48, 48, 48);
constexpr COLORREF GridDotMajor = RGB(66, 66, 66);
constexpr COLORREF NodeBody = RGB(39, 39, 39);
constexpr COLORREF NodeBodyBottom = RGB(34, 34, 34);
constexpr COLORREF NodeOutline = RGB(16, 16, 16);
constexpr COLORREF NodeOutlineSelected = RGB(245, 156, 36);
constexpr COLORREF NodeShadow = RGB(2, 2, 2);
constexpr COLORREF Text = RGB(224, 224, 224);
constexpr COLORREF TextMuted = RGB(176, 176, 176);
constexpr COLORREF Field = RGB(28, 28, 28);
constexpr COLORREF FieldBorder = RGB(18, 18, 18);
constexpr COLORREF FieldFocus = RGB(69, 120, 184);
constexpr COLORREF SliderFill = RGB(58, 91, 126);
constexpr COLORREF SliderFillFocus = RGB(67, 119, 176);
constexpr COLORREF LinkShadow = RGB(0, 0, 0);
constexpr COLORREF LinkFallback = RGB(168, 168, 168);
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

void DrawGraphText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    EnsureMaterialGraphFontRegistered();
    HFONT font = CreateFontW(
        GraphTextLogicalHeight(dc, pointSize),
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
    {
        const ScopedGdiObject selectedFont(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        DrawTextA(dc, text, -1, &rect, static_cast<int>(flags | DT_NOPREFIX));
    }
    DeleteObject(font);
}

void DrawCommandButton(HDC dc, const RECT& rect, const char* label, bool emphasized) {
    const COLORREF fill = emphasized ? RGB(42, 58, 47) : RGB(38, 41, 46);
    const COLORREF border = emphasized ? RGB(83, 122, 91) : RGB(58, 63, 70);
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    DrawText(dc, RECT{ rect.left + 8, rect.top, rect.right - 8, rect.bottom }, label, RGB(221, 226, 232), 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawHeader(HDC dc, const RECT& content, bool dirty, bool infoVisible) {
    const RECT header{ content.left, content.top, content.right, content.top + kHeaderHeight };
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    GdiDrawing::FillRectColor(dc, header, RGB(32, 35, 39));
    GdiDrawing::FillRectColor(dc, RECT{ header.left, header.bottom - 1, header.right, header.bottom }, RGB(13, 14, 16));
    DrawText(dc, RECT{ header.left + kPadding, header.top, layout.infoButton.left - 10, header.bottom }, "Material Editor", RGB(226, 230, 235), 14, FW_SEMIBOLD);
    DrawCommandButton(dc, layout.infoButton, "Info", infoVisible);
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
    if (height <= 0) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        GdiDrawing::FillRectColor(dc, RECT{ rect.left, rect.top + y, rect.right, rect.top + y + 1 }, LerpColor(top, bottom, y, height));
    }
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

void FillRoundedRectAlpha(HDC dc, const RECT& rect, COLORREF color, BYTE alpha, int cornerDiameter) {
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
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

void DrawVerticalGradientClippedToRound(HDC dc, const RECT& rect, const RECT& clip, COLORREF top, COLORREF bottom, int cornerDiameter) {
    const int savedDc = SaveDC(dc);
    HRGN region = CreateRoundRectRgn(clip.left, clip.top, clip.right + 1, clip.bottom + 1, cornerDiameter, cornerDiameter);
    ExtSelectClipRgn(dc, region, RGN_AND);
    DrawVerticalGradient(dc, rect, top, bottom);
    DeleteObject(region);
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

[[nodiscard]] std::vector<std::pair<std::string_view, std::string_view>> GraphInputPins(kb::render::RenderMaterialGraphNodeKind kind);

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
    const int index = GraphInputPinIndex(pin);
    const float scale = NodeUiScale(node, kind);
    const int pinInset = ScaleMetric(6, scale);
    const int headerHeight = ScaleMetric(kGraphNodeHeaderHeight, scale);
    const int count = static_cast<int>(std::max<std::size_t>(1U, GraphInputPins(kind).size()));
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
    if (kind != kb::render::RenderMaterialGraphNodeKind::TextureSample) {
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

[[nodiscard]] std::vector<std::pair<std::string_view, std::string_view>> GraphInputPins(kb::render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return {
            { "baseColor", "Base Color" },
            { "normal", "Normal" },
            { "roughness", "Roughness" },
            { "metallic", "Metallic" },
            { "emissive", "Emissive" },
            { "occlusion", "Occlusion" },
            { "alpha", "Alpha" },
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return { { "texture", "Tex." }, { "uv", "UV" } };
    case kb::render::RenderMaterialGraphNodeKind::Add:
    case kb::render::RenderMaterialGraphNodeKind::Subtract:
    case kb::render::RenderMaterialGraphNodeKind::Multiply:
    case kb::render::RenderMaterialGraphNodeKind::Divide:
    case kb::render::RenderMaterialGraphNodeKind::Minimum:
    case kb::render::RenderMaterialGraphNodeKind::Maximum:
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
    case kb::render::RenderMaterialGraphNodeKind::Distance:
        return { { "a", "A" }, { "b", "B" } };
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
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
    case kb::render::RenderMaterialGraphNodeKind::Length:
    case kb::render::RenderMaterialGraphNodeKind::BreakVector:
        return { { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
        return { { "x", "X" }, { "y", "Y" }, { "z", "Z" }, { "w", "W" } };
    case kb::render::RenderMaterialGraphNodeKind::Step:
        return { { "edge", "Edge" }, { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
        return { { "min", "Min" }, { "max", "Max" }, { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::If:
        return { { "a", "A" }, { "b", "B" }, { "less", "Less" }, { "equal", "Equal" }, { "greater", "Greater" } };
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
        return { { "value", "Value" } };
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2:
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
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return {};
    }
    return {};
}

[[nodiscard]] std::vector<std::pair<std::string_view, std::string_view>> GraphOutputPins(kb::render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
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
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
    case kb::render::RenderMaterialGraphNodeKind::Length:
    case kb::render::RenderMaterialGraphNodeKind::Distance:
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
    case kb::render::RenderMaterialGraphNodeKind::Step:
    case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
    case kb::render::RenderMaterialGraphNodeKind::If:
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
        return { { "rgba", "RGBA" } };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return { { "color", "RGBA" }, { "r", "R" }, { "g", "G" }, { "b", "B" }, { "a", "A" } };
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return { { "texture", "Texture" } };
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        return { { "normal", "Normal" } };
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return { { "uv", "UV" } };
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return {};
    }
    return {};
}

[[nodiscard]] std::string GraphNodeTitle(kb::render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return "Material Output";
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
        return "Value";
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
        return "Vector";
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
        return "Vector";
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
        return "RGB";
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return "Image Texture";
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
        return "Value Parameter";
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
        return "Vector Parameter";
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
        return "RGB Parameter";
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return "Image Parameter";
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
    case kb::render::RenderMaterialGraphNodeKind::Clamp:
        return "Clamp";
    case kb::render::RenderMaterialGraphNodeKind::Lerp:
        return "Mix";
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        return "Normal Map";
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return "Texture Coordinate";
    }
    return "Material Node";
}

[[nodiscard]] COLORREF GraphOutputPinColor(kb::render::RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept;

void DrawGraphGrid(HDC dc, const RECT& canvas, float zoom = 1.0F, int panX = 0, int panY = 0) {
    GdiDrawing::FillRectColor(dc, canvas, BlenderGraphTheme::Canvas);

    const int minorSpacing = std::clamp(ScaleMetric(20, zoom), 8, 80);
    const int majorSpacing = minorSpacing * 4;
    const int minorStartX = canvas.left + ((panX % minorSpacing) + minorSpacing) % minorSpacing;
    const int minorStartY = canvas.top + ((panY % minorSpacing) + minorSpacing) % minorSpacing;
    const int majorStartX = canvas.left + ((panX % majorSpacing) + majorSpacing) % majorSpacing;
    const int majorStartY = canvas.top + ((panY % majorSpacing) + majorSpacing) % majorSpacing;

    for (int x = minorStartX; x < canvas.right; x += minorSpacing) {
        for (int y = minorStartY; y < canvas.bottom; y += minorSpacing) {
            SetPixel(dc, x, y, BlenderGraphTheme::GridDot);
        }
    }

    for (int x = majorStartX; x < canvas.right; x += majorSpacing) {
        for (int y = majorStartY; y < canvas.bottom; y += majorSpacing) {
            GdiDrawing::FillRectColor(dc, RECT{ x - 1, y - 1, x + 2, y + 2 }, BlenderGraphTheme::GridDotMajor);
        }
    }
}

[[nodiscard]] float GraphBezierHandleDistance(POINT from, POINT to) noexcept {
    const float distanceX = static_cast<float>(std::abs(static_cast<int>(to.x - from.x)));
    const float distanceY = static_cast<float>(std::abs(static_cast<int>(to.y - from.y)));
    const float distance = std::sqrt((distanceX * distanceX) + (distanceY * distanceY));
    const float naturalHandle = std::max(distanceX * 0.5F, distance * 0.18F);
    return std::clamp(naturalHandle, 10.0F, 96.0F);
}

void DrawGraphBezier(HDC dc, POINT from, POINT to, COLORREF color, int width) {
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
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
    HDC dc,
    const RECT& fromNode,
    const RECT& toNode,
    std::string_view fromPin,
    std::string_view toPin,
    const kb::render::RenderMaterialGraphNode& fromGraphNode,
    const kb::render::RenderMaterialGraphNode& toGraphNode) {
    const std::vector<std::pair<std::string_view, std::string_view>> outputPins = GraphOutputPins(fromGraphNode.kind);
    std::size_t outputIndex = 0U;
    for (std::size_t index = 0U; index < outputPins.size(); ++index) {
        if (outputPins[index].first == fromPin) {
            outputIndex = index;
            break;
        }
    }
    const POINT from = OutputPinPoint(fromNode, fromGraphNode.kind, outputIndex, outputPins.size());
    const POINT to = InputPinPoint(toNode, toGraphNode.kind, toPin);
    DrawGraphBezier(
        dc,
        from,
        to,
        outputPins.empty() ? BlenderGraphTheme::LinkFallback : GraphOutputPinColor(fromGraphNode.kind, fromPin),
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

        const std::vector<std::pair<std::string_view, std::string_view>> outputPins = GraphOutputPins(fromNode->kind);
        std::size_t outputIndex = 0U;
        for (std::size_t index = 0U; index < outputPins.size(); ++index) {
            if (outputPins[index].first == link.fromPin) {
                outputIndex = index;
                break;
            }
        }
        const POINT from = OutputPinPoint(*fromRect, fromNode->kind, outputIndex, outputPins.size());
        const POINT to = InputPinPoint(*toRect, toNode->kind, link.toPin);
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

void DrawGraphPin(HDC dc, POINT point, COLORREF color, float scale, const RECT& clip, bool tinted = false) {
    const int r = std::max(3, ScaleMetric(kGraphNodePinRadius, scale));
    const COLORREF edge = RGB(9, 9, 9);
    const COLORREF outer = ScaleColor(color, tinted ? 0.64F : 0.78F);
    const COLORREF face = tinted ? color : ScaleColor(color, 1.06F);

    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    const int clipLeft = static_cast<int>(clip.left);
    const int clipTop = static_cast<int>(clip.top);
    const int clipWidth = std::max(0, static_cast<int>(clip.right - clip.left));
    const int clipHeight = std::max(0, static_cast<int>(clip.bottom - clip.top));
    graphics.SetClip(Gdiplus::Rect(
        clipLeft,
        clipTop,
        clipWidth,
        clipHeight));

    const float diameter = static_cast<float>(r * 2);
    const Gdiplus::RectF outerRect{
        static_cast<float>(point.x - r),
        static_cast<float>(point.y - r),
        diameter,
        diameter,
    };
    const float inset = std::max(1.0F, scale);
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
    shadowRect.X += std::max(1.0F, scale);
    shadowRect.Y += std::max(1.0F, scale);
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
}

[[nodiscard]] COLORREF GraphInputPinColor(std::string_view pin) noexcept {
    if (pin == "baseColor") {
        return RGB(220, 170, 48);
    }
    if (pin == "normal") {
        return RGB(92, 157, 214);
    }
    if (pin == "texture") {
        return RGB(184, 143, 214);
    }
    if (pin == "uv") {
        return RGB(92, 157, 214);
    }
    if (pin == "emissive") {
        return RGB(220, 170, 48);
    }
    return RGB(176, 176, 176);
}

[[nodiscard]] COLORREF GraphOutputPinLabelColor(kb::render::RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    if (kind != kb::render::RenderMaterialGraphNodeKind::TextureSample) {
        return BlenderGraphTheme::TextMuted;
    }
    static_cast<void>(pin);
    return BlenderGraphTheme::Text;
}

[[nodiscard]] COLORREF GraphOutputPinColor(kb::render::RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    if (pin == "color" || pin == "rgba") {
        return RGB(220, 170, 48);
    }
    if (pin == "normal") {
        return RGB(92, 157, 214);
    }
    if (pin == "uv" || pin == "xy" || pin == "xyz") {
        return RGB(92, 157, 214);
    }
    if (pin == "texture") {
        return RGB(184, 143, 214);
    }
    if (kind == kb::render::RenderMaterialGraphNodeKind::TextureSample && pin == "r") {
        return RGB(214, 82, 82);
    }
    if (kind == kb::render::RenderMaterialGraphNodeKind::TextureSample && pin == "g") {
        return RGB(92, 178, 88);
    }
    if (kind == kb::render::RenderMaterialGraphNodeKind::TextureSample && pin == "b") {
        return RGB(84, 123, 218);
    }
    return RGB(176, 176, 176);
}

[[nodiscard]] bool GraphOutputPinTinted(kb::render::RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    return kind == kb::render::RenderMaterialGraphNodeKind::TextureSample && (pin == "r" || pin == "g" || pin == "b");
}

[[nodiscard]] COLORREF GraphNodeHeaderColor(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return RGB(98, 58, 51);
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return RGB(91, 74, 47);
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
        return RGB(91, 73, 35);
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
    case kb::render::RenderMaterialGraphNodeKind::Uv:
    case kb::render::RenderMaterialGraphNodeKind::BreakVector:
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
        return RGB(55, 73, 96);
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
        return RGB(68, 68, 68);
    default:
        return RGB(54, 64, 78);
    }
}

void DrawGraphNodeFrame(HDC dc, const RECT& rect, COLORREF body, float scale) {
    const int cornerDiameter = ScaleMetric(kGraphNodeCornerDiameter, scale);
    const int outerSpread = ScaleMetric(7, scale);
    const int middleSpread = ScaleMetric(4, scale);
    const int contactDrop = ScaleMetric(5, scale);
    const int contactSpread = ScaleMetric(3, scale);

    FillRoundedRectAlpha(
        dc,
        RECT{ rect.left - outerSpread, rect.top - outerSpread, rect.right + outerSpread, rect.bottom + outerSpread },
        BlenderGraphTheme::NodeShadow,
        26U,
        cornerDiameter + outerSpread);
    FillRoundedRectAlpha(
        dc,
        RECT{ rect.left - middleSpread, rect.top - middleSpread, rect.right + middleSpread, rect.bottom + middleSpread },
        BlenderGraphTheme::NodeShadow,
        42U,
        cornerDiameter + middleSpread);
    FillRoundedRectAlpha(
        dc,
        RECT{ rect.left - contactSpread, rect.top + contactDrop, rect.right + contactSpread, rect.bottom + contactDrop + contactSpread },
        BlenderGraphTheme::NodeShadow,
        72U,
        cornerDiameter + contactSpread);
    FillRoundedRect(dc, rect, body, ScaleMetric(kGraphNodeCornerDiameter, scale));
}

[[nodiscard]] std::string TextureSampleStableId(const kb::render::RenderMaterialGraphNode& node) {
    if (!node.parameter.stableId.empty()) {
        return node.parameter.stableId;
    }
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture) {
        return "texture" + std::to_string(node.id);
    }
    return "textureSample" + std::to_string(node.id);
}

[[nodiscard]] const kb::render::RenderMaterialGraphNode* TextureValueNodeForDisplay(
    const kb::render::RenderMaterialAssetData& material,
    const kb::render::RenderMaterialGraphNode& node) noexcept {
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture) {
        return &node;
    }
    if (node.kind != kb::render::RenderMaterialGraphNodeKind::TextureSample) {
        return nullptr;
    }
    for (const kb::render::RenderMaterialGraphLink& link : material.graph.links) {
        if (link.toNodeId != node.id || link.toPin != "texture") {
            continue;
        }
        const kb::render::RenderMaterialGraphNode* source = kb::render::FindRenderMaterialGraphNode(material.graph, link.fromNodeId);
        if (source != nullptr && source->kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture && link.fromPin == "texture") {
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

void DrawTexturePreviewBlock(
    HDC dc,
    const RECT& nodeRect,
    const RECT& preview,
    const kb::render::RenderMaterialGraphNode& node,
    const kb::render::RenderMaterialAssetData* material,
    const EditorSceneContext& sceneContext,
    const char* emptyLabel) {
    FillRoundedRect(dc, preview, RGB(24, 24, 24), std::max(4, ScaleMetric(5, NodeUiScale(nodeRect, node.kind))));
    const kb::assets::AssetId assetId = TextureNodeAssetId(material, node);
    const kb::assets::AssetMetadata* metadata = assetId.IsValid()
        ? sceneContext.Scene().Assets().Manager().Registry().Find(assetId)
        : nullptr;
    if (metadata != nullptr) {
        if (const EditorTexturePreviewImage* image = EditorTexturePreviewService::PreviewFor(*metadata); image != nullptr) {
            EditorTexturePreviewService::DrawContain(dc, preview, *image, true);
        } else {
            StrokeRoundedRect(dc, preview, RGB(66, 66, 66), std::max(4, ScaleMetric(5, NodeUiScale(nodeRect, node.kind))));
        }
        if (node.kind != kb::render::RenderMaterialGraphNodeKind::TextureSample) {
            const std::string label = metadata->name.empty() ? metadata->virtualPath.stem().string() : metadata->name;
            const float graphScale = NodeUiScale(nodeRect, node.kind);
            DrawGraphText(dc, RECT{ preview.left, preview.bottom + 6, preview.right, preview.bottom + ScaleMetric(24, graphScale) }, label.c_str(), RGB(218, 226, 238), ScaleMetric(kGraphPinFontSize, graphScale), FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    } else {
        StrokeRoundedRect(dc, preview, RGB(66, 66, 66), std::max(4, ScaleMetric(5, NodeUiScale(nodeRect, node.kind))));
        DrawGraphText(dc, preview, emptyLabel, BlenderGraphTheme::TextMuted, ScaleMetric(kGraphPinFontSize, NodeUiScale(nodeRect, node.kind)), FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void DrawTextureSamplePreview(HDC dc, const RECT& nodeRect, const kb::render::RenderMaterialGraphNode& node, const kb::render::RenderMaterialAssetData* material, const EditorSceneContext& sceneContext) {
    if (node.kind != kb::render::RenderMaterialGraphNodeKind::TextureSample) {
        return;
    }

    DrawTexturePreviewBlock(dc, nodeRect, MaterialEditorPanelTextureSamplePreviewRect(nodeRect), node, material, sceneContext, "Select Texture");
}

void DrawTextureParameterValue(HDC dc, const RECT& nodeRect, const kb::render::RenderMaterialGraphNode& node, const kb::render::RenderMaterialAssetData* material, const EditorSceneContext& sceneContext) {
    if (node.kind != kb::render::RenderMaterialGraphNodeKind::ParameterTexture) {
        return;
    }

    const float scale = NodeScale(nodeRect);
    const RECT valueRect = MaterialEditorPanelTextureParameterRect(nodeRect);
    FillRoundedRect(dc, valueRect, RGB(31, 31, 31), std::max(4, ScaleMetric(6, scale)));
    StrokeRoundedRect(dc, valueRect, RGB(18, 18, 18), std::max(4, ScaleMetric(6, scale)));
    DrawGraphText(
        dc,
        RECT{ valueRect.left + ScaleMetric(10, scale), valueRect.top + ScaleMetric(4, scale), valueRect.right - ScaleMetric(10, scale), valueRect.top + ScaleMetric(24, scale) },
        node.parameter.displayName.empty() ? "Texture" : node.parameter.displayName.c_str(),
        BlenderGraphTheme::TextMuted,
        ScaleMetric(kGraphPinFontSize, NodeUiScale(nodeRect, node.kind)),
        FW_NORMAL,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    const RECT preview{
        valueRect.left + ScaleMetric(10, scale),
        valueRect.top + ScaleMetric(28, scale),
        valueRect.right - ScaleMetric(10, scale),
        valueRect.bottom - ScaleMetric(8, scale),
    };
    DrawTexturePreviewBlock(dc, nodeRect, preview, node, material, sceneContext, "Select Texture");
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
        const char* labels[] = { "X", "Y", "Z" };
        DrawConstantVectorFields(
            dc,
            nodeRect,
            value,
            sceneContext.MaterialEditor().GraphConstantInlineEditBuffer(),
            3U,
            labels,
            editing,
            scale);
        return;
    }

    RECT textRect{ valueRect.left + ScaleMetric(13, scale), valueRect.top, valueRect.right - ScaleMetric(10, scale), valueRect.bottom };
    RECT colorSwatch{};
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor) {
        colorSwatch = RECT{
            valueRect.left + ScaleMetric(8, scale),
            valueRect.top + ScaleMetric(5, scale),
            valueRect.left + ScaleMetric(28, scale),
            valueRect.bottom - ScaleMetric(5, scale),
        };
        textRect.left = colorSwatch.right + ScaleMetric(11, scale);
    }
    const float colorSliderValue = node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor
        ? ((value.numbers[0] + value.numbers[1] + value.numbers[2]) / 3.0F)
        : value.numbers[0];
    DrawGraphValueSliderField(dc, valueRect, textRect, valueText.c_str(), colorSliderValue, editing, scale);
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor) {
        const COLORREF color = RGB(
            std::clamp(static_cast<int>(value.numbers[0] * 255.0F), 0, 255),
            std::clamp(static_cast<int>(value.numbers[1] * 255.0F), 0, 255),
            std::clamp(static_cast<int>(value.numbers[2] * 255.0F), 0, 255));
        FillRoundedRect(dc, colorSwatch, color, std::max(3, ScaleMetric(4, scale)));
        StrokeRoundedRect(dc, colorSwatch, RGB(18, 18, 18), std::max(3, ScaleMetric(4, scale)));
    }
}

void DrawGraphNode(HDC dc, const RECT& rect, const RECT& clip, const kb::render::RenderMaterialGraphNode& node, bool selected, const kb::render::RenderMaterialAssetData* material, const EditorSceneContext& sceneContext) {
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
    DrawGraphNodeFrame(dc, rect, body, scale);
    const RECT inner{ rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2 };
    DrawVerticalGradientClippedToRound(dc, RECT{ inner.left, rect.top + headerHeight, inner.right, inner.bottom }, inner, bodyTop, bodyBottom, std::max(2, cornerDiameter - 2));
    DrawVerticalGradientClippedToRound(dc, RECT{ inner.left, inner.top, inner.right, rect.top + headerHeight }, inner, headerTop, headerBottom, std::max(2, cornerDiameter - 2));
    GdiDrawing::FillRectColor(dc, RECT{ rect.left + 2, rect.top + headerHeight, rect.right - 2, rect.top + headerHeight + 1 }, RGB(18, 18, 18));
    StrokeRoundedRect(dc, rect, border, cornerDiameter, selected ? 2 : 1);

    const std::string title = GraphNodeTitle(node.kind);
    DrawGraphText(dc, RECT{ rect.left + ScaleMetric(12, scale), rect.top, rect.right - ScaleMetric(12, scale), rect.top + headerHeight }, title.c_str(), RGB(242, 242, 242), ScaleMetric(kGraphTitleFontSize, scale), FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextureSamplePreview(dc, rect, node, material, sceneContext);
    DrawTextureParameterValue(dc, rect, node, material, sceneContext);
    DrawConstantValue(dc, rect, node, sceneContext);

    const std::vector<std::pair<std::string_view, std::string_view>> inputPins = GraphInputPins(node.kind);
    if (!inputPins.empty()) {
        for (std::size_t index = 0; index < inputPins.size(); ++index) {
            const POINT scaledPin = InputPinPoint(rect, node.kind, inputPins[index].first);
            DrawGraphPin(dc, scaledPin, GraphInputPinColor(inputPins[index].first), scale, clip);
            const bool textureSample = node.kind == kb::render::RenderMaterialGraphNodeKind::TextureSample;
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
    const std::vector<std::pair<std::string_view, std::string_view>> outputPins = GraphOutputPins(node.kind);
    for (std::size_t index = 0U; index < outputPins.size(); ++index) {
        const POINT output = OutputPinPoint(rect, node.kind, index, outputPins.size());
        DrawGraphPin(
            dc,
            output,
            GraphOutputPinColor(node.kind, outputPins[index].first),
            scale,
            clip,
            GraphOutputPinTinted(node.kind, outputPins[index].first));
        RECT outputLabelRect{
            rect.left + ScaleMetric(8, scale),
            output.y - ScaleMetric(11, scale),
            output.x - pinRadius - ScaleMetric(8, scale),
            output.y + ScaleMetric(11, scale),
        };
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::TextureSample) {
            const RECT texturePreview = MaterialEditorPanelTextureSamplePreviewRect(rect);
            outputLabelRect.left = texturePreview.right + ScaleMetric(8, scale);
            outputLabelRect.right = output.x - pinRadius - ScaleMetric(8, scale);
        }
        if (MaterialEditorPanelIsConstantNode(node.kind)) {
            RECT valueRect = MaterialEditorPanelConstantValueRect(rect);
            if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector2) {
                valueRect = MaterialEditorPanelConstantVectorFieldsBounds(rect, 2U);
            } else if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector) {
                valueRect = MaterialEditorPanelConstantVectorFieldsBounds(rect, 3U);
            }
            outputLabelRect.left = std::max(outputLabelRect.left, valueRect.right + ScaleMetric(8, scale));
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
        const std::vector<std::pair<std::string_view, std::string_view>> outputPins = GraphOutputPins(node->kind);
        std::size_t pinIndex = 0U;
        for (std::size_t index = 0U; index < outputPins.size(); ++index) {
            if (outputPins[index].first == sceneContext.MaterialGraphPinConnectionPin()) {
                pinIndex = index;
                break;
            }
        }
        anchor = OutputPinPoint(*nodeRect, node->kind, pinIndex, outputPins.size());
    } else {
        const std::vector<std::pair<std::string_view, std::string_view>> inputPins = GraphInputPins(node->kind);
        std::string_view pinName = inputPins.empty() ? std::string_view{} : inputPins.front().first;
        for (const auto& inputPin : inputPins) {
            if (inputPin.first == sceneContext.MaterialGraphPinConnectionPin()) {
                pinName = inputPin.first;
                break;
            }
        }
        anchor = InputPinPoint(*nodeRect, node->kind, pinName);
    }

    const POINT cursor{ sceneContext.MaterialGraphPinConnectionX(), sceneContext.MaterialGraphPinConnectionY() };
    DrawGraphBezier(
        dc,
        sceneContext.MaterialGraphPinConnectionIsOutput() ? anchor : cursor,
        sceneContext.MaterialGraphPinConnectionIsOutput() ? cursor : anchor,
        sceneContext.MaterialGraphPinConnectionIsOutput()
            ? GraphOutputPinColor(node->kind, sceneContext.MaterialGraphPinConnectionPin())
            : GraphInputPinColor(sceneContext.MaterialGraphPinConnectionPin()),
        std::clamp(ScaleMetric(3, sceneContext.MaterialGraphZoom()), 1, 3));
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
    DrawText(dc, RECT{ search.left + 10, search.top, search.right - 10, search.bottom }, "Search nodes...", RGB(172, 184, 198), 10);

    int y = menu.top + kMaterialEditorGraphMenuPadding + kMaterialEditorGraphMenuSearchHeight + kMaterialEditorGraphMenuPadding;
    const bool hasSelectedNode = sceneContext.SelectedMaterialGraphNodeId() != 0U;
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

        const std::vector<MaterialEditorGraphMenuCommand> commands = MaterialEditorGraphContextMenuCommands(categoryIndex);
        for (const MaterialEditorGraphMenuCommand command : commands) {
            const bool enabled = MaterialEditorGraphContextMenuCommandEnabled(command, hasSelectedNode);
            const bool commandHovered = sceneContext.IsMaterialGraphContextMenuCommandHovered(categoryIndex, command);
            const RECT commandFill{ menu.left + 8, y, menu.right - 8, y + kMaterialEditorGraphMenuCommandHeight };
            GdiDrawing::FillRectColor(
                dc,
                commandFill,
                commandHovered
                    ? ProjectFilesPanelDrawing::Blend(RGB(24, 27, 33), RGB(166, 178, 193), 14)
                    : RGB(24, 27, 33));
            const RECT commandRow{ menu.left + 32, y, menu.right - 12, y + kMaterialEditorGraphMenuCommandHeight };
            DrawText(
                dc,
                commandRow,
                std::string{ MaterialEditorGraphContextMenuCommandName(command) }.c_str(),
                enabled ? (commandHovered ? RGB(248, 250, 252) : RGB(213, 222, 235)) : RGB(102, 112, 126),
                10,
                commandHovered ? FW_SEMIBOLD : FW_NORMAL);
            y += kMaterialEditorGraphMenuCommandHeight;
        }
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
    for (const kb::render::RenderMaterialGraphLink& link : graphView.links) {
        const std::optional<RECT> from = MaterialEditorPanelRenderer::GraphNodeRect(content, graphView, link.fromNodeId, sceneContext, assetId);
        const std::optional<RECT> to = MaterialEditorPanelRenderer::GraphNodeRect(content, graphView, link.toNodeId, sceneContext, assetId);
        if (from.has_value() && to.has_value()) {
            const kb::render::RenderMaterialGraphNode* fromNode = kb::render::FindRenderMaterialGraphNode(graphView, link.fromNodeId);
            const kb::render::RenderMaterialGraphNode* toNode = kb::render::FindRenderMaterialGraphNode(graphView, link.toNodeId);
            if (fromNode != nullptr && toNode != nullptr) {
                DrawGraphLink(dc, *from, *to, link.fromPin, link.toPin, *fromNode, *toNode);
            }
        }
    }
    for (const kb::render::RenderMaterialGraphNode& node : graphView.nodes) {
        const std::optional<RECT> nodeRect = MaterialEditorPanelRenderer::GraphNodeRect(content, graphView, node.id, sceneContext, assetId);
        if (nodeRect.has_value()) {
            DrawGraphNode(dc, *nodeRect, layout.graphCanvas, node, node.id == selectedNodeId, graph.nodes.empty() ? nullptr : &material, sceneContext);
        }
    }
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
            return MaterialEditorDocumentView{
                .material = sceneContext.MaterialEditor().WorkingCopy(),
                .assetKind = "Material",
                .parentMaterialAssetId = {},
                .diagnostics = sceneContext.MaterialEditor().Diagnostics(),
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
    const std::filesystem::path parentPath = parentMetadata != nullptr ? ResolveAssetPath(manager, *parentMetadata) : std::filesystem::path{};
    kb::render::RenderMaterialAssetParseResult parent = parentPath.empty()
        ? kb::render::RenderMaterialAssetParseResult{}
        : kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(parentPath, instance.asset->parentMaterialAssetId);
    if (parentPath.empty()) {
        diagnostics.push_back("Error missing_parent_material: Parent material asset could not be resolved.");
        hasError = true;
    } else {
        AppendMaterialDiagnostics(diagnostics, hasError, parent);
    }
    return MaterialEditorDocumentView{
        .material = std::move(parent.asset),
        .assetKind = "Material Instance",
        .parentMaterialAssetId = instance.asset->parentMaterialAssetId,
        .diagnostics = std::move(diagnostics),
        .hasErrorDiagnostic = hasError,
    };
}

void DrawGraphOverlay(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
}

void DrawPreviewOverlay(HDC dc, const MaterialEditorPanelLayout& layout, const EditorMaterialPreviewTelemetry& telemetry) {
    DrawGraphOverlay(dc, layout.previewFrame, RGB(9, 10, 12), RGB(54, 58, 66));
    DrawText(dc, layout.previewFrame, telemetry.materialLoaded ? "Preview" : "Preview unavailable", RGB(86, 92, 100), 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
    DrawPreviewOverlay(dc, layout, telemetry);
    DrawDiagnosticsPanel(dc, layout, *document);
    if (sceneContext.MaterialEditor().InfoPanelVisible()) {
        MaterialEditorPanelDetailsRows details = MaterialEditorPanelRenderer::DetailsRows(
            sceneContext.MaterialEditor().Parameters(),
            sceneContext.MaterialEditor().SelectedNodeId());
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
    DrawHeader(dc, content, sceneContext.HasDirtyMaterialAssetEdit(), sceneContext.MaterialEditor().InfoPanelVisible());

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
