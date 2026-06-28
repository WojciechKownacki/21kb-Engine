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
constexpr int kGraphNodePinRadius = 8;
constexpr int kGraphNodeCornerDiameter = 18;

void DrawText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize = 12, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
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
    if (pin == "base") return 0;
    if (pin == "exponent") return 1;
    if (pin == "min") return 1;
    if (pin == "max") return 2;
    if (pin == "t") return 2;
    if (pin == "color") return 0;
    return 0;
}

[[nodiscard]] POINT InputPinPoint(const RECT& node, std::string_view pin) noexcept {
    const int index = GraphInputPinIndex(pin);
    const float scale = NodeScale(node);
    return POINT{
        node.left,
        node.top + ScaleMetric(kGraphNodeHeaderHeight, scale) + ScaleMetric(kGraphNodeBodyTopPadding, scale) + (index * ScaleMetric(kGraphNodePinRowHeight, scale)) + (ScaleMetric(kGraphNodePinRowHeight, scale) / 2),
    };
}

[[nodiscard]] POINT OutputPinPoint(const RECT& node) noexcept {
    return POINT{ node.right, node.top + (RectHeight(node) / 2) };
}

[[nodiscard]] POINT OutputPinPoint(const RECT& node, std::size_t index, std::size_t count) noexcept {
    if (count <= 1U) {
        return OutputPinPoint(node);
    }
    const float scale = NodeScale(node);
    const int rowHeight = ScaleMetric(kGraphNodePinRowHeight, scale);
    const int total = static_cast<int>(count) * rowHeight;
    return POINT{
        node.right,
        node.top + (RectHeight(node) / 2) - (total / 2) + (static_cast<int>(index) * rowHeight) + (rowHeight / 2),
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
        return { { "texture", "Texture" }, { "uv", "UV" } };
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
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
        return { { "xyz", "XYZ" } };
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
        return { { "rgba", "RGBA" } };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return { { "color", "Color" }, { "r", "R" }, { "g", "G" }, { "b", "B" }, { "a", "A" } };
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
        return "Constant Scalar";
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
        return "Constant Vector";
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
        return "Constant Color";
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return "Texture Sample";
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
        return "Scalar Parameter";
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
        return "Vector Parameter";
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
        return "Color Parameter";
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return "Texture Parameter";
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
        return "Lerp";
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        return "Normal Unpack";
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return "UV";
    }
    return "Material Node";
}

void DrawGraphGrid(HDC dc, const RECT& canvas, float zoom = 1.0F, int panX = 0, int panY = 0) {
    GdiDrawing::FillRectColor(dc, canvas, RGB(17, 18, 20));

    const int minorSpacing = std::clamp(ScaleMetric(32, zoom), 10, 96);
    const int majorSpacing = minorSpacing * 4;
    const int minorStartX = canvas.left + ((panX % minorSpacing) + minorSpacing) % minorSpacing;
    const int minorStartY = canvas.top + ((panY % minorSpacing) + minorSpacing) % minorSpacing;
    const int majorStartX = canvas.left + ((panX % majorSpacing) + majorSpacing) % majorSpacing;
    const int majorStartY = canvas.top + ((panY % majorSpacing) + majorSpacing) % majorSpacing;

    HPEN minorPen = CreatePen(PS_SOLID, 1, RGB(31, 34, 38));
    {
        const ScopedGdiObject selectedPen(dc, minorPen);
        for (int x = minorStartX; x < canvas.right; x += minorSpacing) {
            MoveToEx(dc, x, canvas.top, nullptr);
            LineTo(dc, x, canvas.bottom);
        }
        for (int y = minorStartY; y < canvas.bottom; y += minorSpacing) {
            MoveToEx(dc, canvas.left, y, nullptr);
            LineTo(dc, canvas.right, y);
        }
    }
    DeleteObject(minorPen);

    HPEN majorPen = CreatePen(PS_SOLID, 1, RGB(42, 45, 50));
    {
        const ScopedGdiObject selectedPen(dc, majorPen);
        for (int x = majorStartX; x < canvas.right; x += majorSpacing) {
            MoveToEx(dc, x, canvas.top, nullptr);
            LineTo(dc, x, canvas.bottom);
        }
        for (int y = majorStartY; y < canvas.bottom; y += majorSpacing) {
            MoveToEx(dc, canvas.left, y, nullptr);
            LineTo(dc, canvas.right, y);
        }
    }
    DeleteObject(majorPen);
}

void DrawGraphBezier(HDC dc, POINT from, POINT to, COLORREF color, int width) {
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::Pen pen(ToGdiplusColor(color), static_cast<Gdiplus::REAL>(std::max(1, width)));
    const int controlDistance = std::abs(static_cast<int>(to.x - from.x)) / 2;
    const float dx = static_cast<float>(std::max(72, controlDistance));
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
    graphics.DrawPath(&pen, &path);
}

void DrawGraphLink(HDC dc, const RECT& fromNode, const RECT& toNode, std::string_view fromPin, std::string_view toPin, const kb::render::RenderMaterialGraphNode& fromGraphNode) {
    const std::vector<std::pair<std::string_view, std::string_view>> outputPins = GraphOutputPins(fromGraphNode.kind);
    std::size_t outputIndex = 0U;
    for (std::size_t index = 0U; index < outputPins.size(); ++index) {
        if (outputPins[index].first == fromPin) {
            outputIndex = index;
            break;
        }
    }
    const POINT from = OutputPinPoint(fromNode, outputIndex, outputPins.size());
    const POINT to = InputPinPoint(toNode, toPin);
    DrawGraphBezier(dc, from, to, RGB(202, 207, 216), std::max(2, ScaleMetric(3, NodeScale(fromNode))));
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
    const double controlDistance = std::abs(static_cast<int>(to.x - from.x)) / 2.0;
    const double dx = static_cast<double>(std::max(72, static_cast<int>(controlDistance)));
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
        const POINT from = OutputPinPoint(*fromRect, outputIndex, outputPins.size());
        const POINT to = InputPinPoint(*toRect, link.toPin);
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

void DrawGraphPin(HDC dc, POINT point, COLORREF color, float scale, const RECT& clip) {
    const int r = ScaleMetric(kGraphNodePinRadius, scale);
    const COLORREF edge = ScaleColor(color, 0.74F);
    const COLORREF face = LerpColor(color, RGB(246, 248, 250), 1, 6);
    const COLORREF faceTop = LerpColor(color, RGB(255, 255, 255), 1, 3);

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
    const Gdiplus::RectF pinRect{
        static_cast<float>(point.x - r),
        static_cast<float>(point.y - r),
        diameter,
        diameter,
    };
    Gdiplus::SolidBrush faceBrush(ToGdiplusColor(face));
    Gdiplus::Pen edgePen(ToGdiplusColor(edge), std::max(1.0F, scale));
    graphics.FillEllipse(&faceBrush, pinRect);
    graphics.DrawEllipse(&edgePen, pinRect);

    const Gdiplus::RectF highlightRect{
        static_cast<float>(point.x - r + ScaleMetric(2, scale)),
        static_cast<float>(point.y - r + ScaleMetric(2, scale)),
        static_cast<float>(std::max(1, r + ScaleMetric(2, scale))),
        static_cast<float>(std::max(1, r / 2 + ScaleMetric(2, scale))),
    };
    Gdiplus::SolidBrush highlightBrush(ToGdiplusColor(faceTop, 150U));
    graphics.FillEllipse(&highlightBrush, highlightRect);
}

[[nodiscard]] COLORREF GraphInputPinColor(std::string_view pin) noexcept {
    if (pin == "baseColor") {
        return RGB(239, 202, 72);
    }
    if (pin == "normal") {
        return RGB(89, 206, 122);
    }
    return RGB(197, 205, 216);
}

void DrawGraphNodeFrame(HDC dc, const RECT& rect, COLORREF body) {
    const float scale = NodeScale(rect);
    FillRoundedRect(dc, RECT{ rect.left + ScaleMetric(6, scale), rect.top + ScaleMetric(6, scale), rect.right + ScaleMetric(6, scale), rect.bottom + ScaleMetric(6, scale) }, RGB(8, 9, 11), ScaleMetric(kGraphNodeCornerDiameter + 4, scale));
    FillRoundedRect(dc, RECT{ rect.left + ScaleMetric(3, scale), rect.top + ScaleMetric(3, scale), rect.right + ScaleMetric(3, scale), rect.bottom + ScaleMetric(3, scale) }, RGB(13, 15, 18), ScaleMetric(kGraphNodeCornerDiameter + 2, scale));
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
    GdiDrawing::FillRectColor(dc, preview, RGB(11, 13, 16));
    const kb::assets::AssetId assetId = TextureNodeAssetId(material, node);
    const kb::assets::AssetMetadata* metadata = assetId.IsValid()
        ? sceneContext.Scene().Assets().Manager().Registry().Find(assetId)
        : nullptr;
    if (metadata != nullptr) {
        if (const EditorTexturePreviewImage* image = EditorTexturePreviewService::PreviewFor(*metadata); image != nullptr) {
            EditorTexturePreviewService::DrawContain(dc, preview, *image, true);
        } else {
            GdiDrawing::DrawSharpFrame(dc, preview, RGB(17, 20, 24), RGB(54, 62, 72));
        }
        const std::string label = metadata->name.empty() ? metadata->virtualPath.stem().string() : metadata->name;
        DrawText(dc, RECT{ preview.left, preview.bottom + 6, preview.right, preview.bottom + ScaleMetric(24, NodeScale(nodeRect)) }, label.c_str(), RGB(218, 226, 238), std::max(10, ScaleMetric(10, NodeScale(nodeRect))), FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        GdiDrawing::DrawSharpFrame(dc, preview, RGB(17, 20, 24), RGB(54, 62, 72));
        DrawText(dc, preview, emptyLabel, RGB(172, 184, 198), 10, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
    GdiDrawing::DrawSharpFrame(dc, valueRect, RGB(16, 18, 23), RGB(54, 63, 74));
    DrawText(
        dc,
        RECT{ valueRect.left + ScaleMetric(10, scale), valueRect.top + ScaleMetric(4, scale), valueRect.right - ScaleMetric(10, scale), valueRect.top + ScaleMetric(24, scale) },
        node.parameter.displayName.empty() ? "Texture" : node.parameter.displayName.c_str(),
        RGB(178, 189, 202),
        std::max(9, ScaleMetric(10, scale)),
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

void DrawConstantValue(HDC dc, const RECT& nodeRect, const kb::render::RenderMaterialGraphNode& node) {
    if (!MaterialEditorPanelIsConstantNode(node.kind)) {
        return;
    }

    const float scale = NodeScale(nodeRect);
    const RECT valueRect = MaterialEditorPanelConstantValueRect(nodeRect);
    GdiDrawing::DrawSharpFrame(dc, valueRect, RGB(16, 18, 23), RGB(54, 63, 74));

    const MaterialEditorParameterValue value = MaterialEditorPanelConstantParameterValue(node.kind, node.parameter.defaultValueHint);
    const std::string valueText = MaterialEditorPanelParameterValueText(value);
    const std::string displayName = node.parameter.displayName.empty() ? std::string{ "Value" } : node.parameter.displayName;
    DrawText(
        dc,
        RECT{ valueRect.left + ScaleMetric(10, scale), valueRect.top + ScaleMetric(4, scale), valueRect.right - ScaleMetric(10, scale), valueRect.top + ScaleMetric(24, scale) },
        displayName.c_str(),
        RGB(178, 189, 202),
        std::max(9, ScaleMetric(10, scale)),
        FW_NORMAL,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT textRect{ valueRect.left + ScaleMetric(10, scale), valueRect.top + ScaleMetric(26, scale), valueRect.right - ScaleMetric(10, scale), valueRect.bottom - ScaleMetric(4, scale) };
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor) {
        const RECT swatch{
            valueRect.left + ScaleMetric(10, scale),
            valueRect.top + ScaleMetric(29, scale),
            valueRect.left + ScaleMetric(42, scale),
            valueRect.bottom - ScaleMetric(8, scale),
        };
        const COLORREF color = RGB(
            std::clamp(static_cast<int>(value.numbers[0] * 255.0F), 0, 255),
            std::clamp(static_cast<int>(value.numbers[1] * 255.0F), 0, 255),
            std::clamp(static_cast<int>(value.numbers[2] * 255.0F), 0, 255));
        GdiDrawing::DrawSharpFrame(dc, swatch, color, RGB(83, 91, 103));
        textRect.left = swatch.right + ScaleMetric(8, scale);
    }
    DrawText(
        dc,
        textRect,
        valueText.c_str(),
        RGB(235, 240, 248),
        std::max(10, ScaleMetric(13, scale)),
        FW_SEMIBOLD,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawGraphNode(HDC dc, const RECT& rect, const RECT& clip, const kb::render::RenderMaterialGraphNode& node, bool selected, const kb::render::RenderMaterialAssetData* material, const EditorSceneContext& sceneContext) {
    const bool outputNode = node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput;
    const float scale = NodeScale(rect);
    const int headerHeight = ScaleMetric(kGraphNodeHeaderHeight, scale);
    const int cornerDiameter = ScaleMetric(kGraphNodeCornerDiameter, scale);
    const int pinRadius = ScaleMetric(kGraphNodePinRadius, scale);
    const COLORREF body = outputNode ? RGB(23, 27, 34) : RGB(23, 25, 30);
    const COLORREF bodyTop = outputNode ? RGB(48, 53, 64) : RGB(43, 46, 54);
    const COLORREF bodyBottom = outputNode ? RGB(19, 22, 28) : RGB(22, 24, 29);
    const COLORREF headerTop = outputNode ? RGB(168, 88, 86) : RGB(63, 70, 82);
    const COLORREF headerBottom = outputNode ? RGB(103, 56, 57) : RGB(39, 46, 56);
    const COLORREF border = selected ? RGB(46, 52, 62) : RGB(10, 12, 15);
    DrawGraphNodeFrame(dc, rect, body);
    const RECT inner{ rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2 };
    DrawVerticalGradientClippedToRound(dc, RECT{ inner.left, rect.top + headerHeight, inner.right, inner.bottom }, inner, bodyTop, bodyBottom, std::max(2, cornerDiameter - 2));
    DrawVerticalGradientClippedToRound(dc, RECT{ inner.left, inner.top, inner.right, rect.top + headerHeight }, inner, headerTop, headerBottom, std::max(2, cornerDiameter - 2));
    GdiDrawing::FillRectColor(dc, RECT{ rect.left + 2, rect.top + headerHeight, rect.right - 2, rect.top + headerHeight + 1 }, RGB(16, 18, 22));
    GdiDrawing::FillRectAlpha(dc, RECT{ rect.left + 3, rect.top + 2, rect.right - 3, rect.top + std::max(3, ScaleMetric(4, scale)) }, RGB(255, 255, 255), 24);
    StrokeRoundedRect(dc, rect, border, cornerDiameter, selected ? 2 : 1);

    const std::string title = GraphNodeTitle(node.kind);
    const std::string subtitle = outputNode ? std::string{ "Surface Inputs" } : ("#" + std::to_string(node.id));
    DrawText(dc, RECT{ rect.left + ScaleMetric(16, scale), rect.top + 1, rect.right - ScaleMetric(16, scale), rect.top + headerHeight }, title.c_str(), RGB(248, 249, 251), std::max(9, ScaleMetric(16, scale)), FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (!outputNode) {
        DrawText(dc, RECT{ rect.left + ScaleMetric(18, scale), rect.top + headerHeight + ScaleMetric(5, scale), rect.right - ScaleMetric(12, scale), rect.top + headerHeight + ScaleMetric(18, scale) }, subtitle.c_str(), RGB(129, 140, 150), std::max(7, ScaleMetric(8, scale)));
    }
    DrawTextureSamplePreview(dc, rect, node, material, sceneContext);
    DrawTextureParameterValue(dc, rect, node, material, sceneContext);
    DrawConstantValue(dc, rect, node);

    const std::vector<std::pair<std::string_view, std::string_view>> inputPins = GraphInputPins(node.kind);
    if (!inputPins.empty()) {
        for (std::size_t index = 0; index < inputPins.size(); ++index) {
            const POINT pin = InputPinPoint(rect, inputPins[index].first);
            DrawGraphPin(dc, pin, GraphInputPinColor(inputPins[index].first), scale, clip);
            DrawText(dc, RECT{ rect.left + pinRadius + ScaleMetric(16, scale), pin.y - ScaleMetric(16, scale), rect.right - ScaleMetric(18, scale), pin.y + ScaleMetric(16, scale) }, std::string{ inputPins[index].second }.c_str(), RGB(198, 205, 218), std::max(10, ScaleMetric(outputNode ? 17 : 13, scale)));
        }
    }
    const std::vector<std::pair<std::string_view, std::string_view>> outputPins = GraphOutputPins(node.kind);
    for (std::size_t index = 0U; index < outputPins.size(); ++index) {
        const POINT output = OutputPinPoint(rect, index, outputPins.size());
        DrawGraphPin(dc, output, RGB(91, 139, 206), scale, clip);
        DrawText(
            dc,
            RECT{ rect.left + ScaleMetric(8, scale), output.y - ScaleMetric(10, scale), output.x - pinRadius - ScaleMetric(8, scale), output.y + ScaleMetric(10, scale) },
            std::string{ outputPins[index].second }.c_str(),
            RGB(178, 188, 199),
            node.kind == kb::render::RenderMaterialGraphNodeKind::TextureSample
                ? std::max(10, ScaleMetric(13, scale))
                : std::max(7, ScaleMetric(8, scale)),
            FW_NORMAL,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
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
        anchor = OutputPinPoint(*nodeRect, pinIndex, outputPins.size());
    } else {
        const std::vector<std::pair<std::string_view, std::string_view>> inputPins = GraphInputPins(node->kind);
        std::string_view pinName = inputPins.empty() ? std::string_view{} : inputPins.front().first;
        for (const auto& inputPin : inputPins) {
            if (inputPin.first == sceneContext.MaterialGraphPinConnectionPin()) {
                pinName = inputPin.first;
                break;
            }
        }
        anchor = InputPinPoint(*nodeRect, pinName);
    }

    const POINT cursor{ sceneContext.MaterialGraphPinConnectionX(), sceneContext.MaterialGraphPinConnectionY() };
    DrawGraphBezier(
        dc,
        sceneContext.MaterialGraphPinConnectionIsOutput() ? anchor : cursor,
        sceneContext.MaterialGraphPinConnectionIsOutput() ? cursor : anchor,
        RGB(226, 231, 238),
        3);
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
            if (fromNode != nullptr) {
                DrawGraphLink(dc, *from, *to, link.fromPin, link.toPin, *fromNode);
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
