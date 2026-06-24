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
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
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
#include <string>
#include <utility>

namespace kb::editor {
namespace {

constexpr int kHeaderHeight = MaterialEditorPanelMetrics::HeaderHeight;
constexpr int kPadding = MaterialEditorPanelMetrics::Padding;
constexpr int kGraphNodeHeaderHeight = 46;
constexpr int kGraphNodeBodyTopPadding = 18;
constexpr int kGraphNodePinRowHeight = 38;
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

void DrawHeader(HDC dc, const RECT& content, bool dirty) {
    const RECT header{ content.left, content.top, content.right, content.top + kHeaderHeight };
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    GdiDrawing::FillRectColor(dc, header, RGB(32, 35, 39));
    GdiDrawing::FillRectColor(dc, RECT{ header.left, header.bottom - 1, header.right, header.bottom }, RGB(13, 14, 16));
    DrawText(dc, RECT{ header.left + kPadding, header.top, layout.saveButton.left - 10, header.bottom }, "Material Editor", RGB(226, 230, 235), 14, FW_SEMIBOLD);
    DrawCommandButton(dc, layout.saveButton, "Save", dirty);
    DrawCommandButton(dc, layout.revertButton, "Revert", false);
    DrawCommandButton(dc, layout.validateButton, "Validate", false);
    if (dirty) {
        DrawText(dc, RECT{ header.left + kPadding, header.top, layout.saveButton.left - 10, header.bottom }, "Unsaved changes", RGB(223, 178, 91), 11, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
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
    if (pin == "uv") return 0;
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

[[nodiscard]] std::string GraphNodeTitle(kb::render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return "Material Output";
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
        return "Constant Scalar";
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
        return "Constant Color";
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return "Texture Sample";
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
        return "Scalar Parameter";
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
        return "Color Parameter";
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return "Texture Parameter";
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

void DrawGraphLink(HDC dc, const RECT& fromNode, const RECT& toNode, std::string_view toPin) {
    const POINT from = OutputPinPoint(fromNode);
    const POINT to = InputPinPoint(toNode, toPin);
    const int width = std::max(1, ScaleMetric(2, NodeScale(fromNode)));
    HPEN linkPen = CreatePen(PS_SOLID, width, RGB(202, 207, 216));
    {
        const ScopedGdiObject selectedPen(dc, linkPen);
        MoveToEx(dc, from.x, from.y, nullptr);
        const int midpoint = from.x + ((to.x - from.x) / 2);
        LineTo(dc, midpoint, from.y);
        LineTo(dc, midpoint, to.y);
        LineTo(dc, to.x, to.y);
    }
    DeleteObject(linkPen);
}

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

void DrawGraphNode(HDC dc, const RECT& rect, const RECT& clip, const kb::render::RenderMaterialGraphNode& node, bool selected) {
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

    if (outputNode) {
        const std::array<std::pair<std::string_view, std::string_view>, 7> pins{
            std::pair<std::string_view, std::string_view>{ "baseColor", "Base Color" },
            std::pair<std::string_view, std::string_view>{ "normal", "Normal" },
            std::pair<std::string_view, std::string_view>{ "roughness", "Roughness" },
            std::pair<std::string_view, std::string_view>{ "metallic", "Metallic" },
            std::pair<std::string_view, std::string_view>{ "emissive", "Emissive" },
            std::pair<std::string_view, std::string_view>{ "occlusion", "Occlusion" },
            std::pair<std::string_view, std::string_view>{ "alpha", "Alpha" },
        };
        for (std::size_t index = 0; index < pins.size(); ++index) {
            const POINT pin = InputPinPoint(rect, pins[index].first);
            DrawGraphPin(dc, pin, GraphInputPinColor(pins[index].first), scale, clip);
            DrawText(dc, RECT{ rect.left + pinRadius + ScaleMetric(16, scale), pin.y - ScaleMetric(16, scale), rect.right - ScaleMetric(18, scale), pin.y + ScaleMetric(16, scale) }, std::string{ pins[index].second }.c_str(), RGB(198, 205, 218), std::max(10, ScaleMetric(17, scale)));
        }
    } else {
        const POINT output = OutputPinPoint(rect);
        DrawGraphPin(dc, output, RGB(91, 139, 206), scale, clip);
        DrawText(dc, RECT{ rect.left + ScaleMetric(8, scale), output.y - ScaleMetric(10, scale), output.x - pinRadius - ScaleMetric(8, scale), output.y + ScaleMetric(10, scale) }, "out", RGB(178, 188, 199), std::max(7, ScaleMetric(8, scale)), FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawGraphCanvas(HDC dc, const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, const EditorSceneContext& sceneContext, kb::assets::AssetId assetId, std::uint32_t selectedNodeId) {
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    DrawGraphGrid(dc, layout.graphCanvas, sceneContext.MaterialGraphZoom(), sceneContext.MaterialGraphPanX(), sceneContext.MaterialGraphPanY());

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
            DrawGraphLink(dc, *from, *to, link.toPin);
        }
    }
    for (const kb::render::RenderMaterialGraphNode& node : graphView.nodes) {
        const std::optional<RECT> nodeRect = MaterialEditorPanelRenderer::GraphNodeRect(content, graphView, node.id, sceneContext, assetId);
        if (nodeRect.has_value()) {
            DrawGraphNode(dc, *nodeRect, layout.graphCanvas, node, node.id == selectedNodeId);
        }
    }
    RestoreDC(dc, savedDc);
}

struct MaterialEditorDocumentView {
    kb::render::RenderMaterialAssetData material{};
    std::string assetKind;
    kb::assets::AssetId parentMaterialAssetId{};
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

[[nodiscard]] std::optional<MaterialEditorDocumentView> ReadDocumentView(
    const EditorSceneContext& sceneContext,
    const kb::assets::AssetMetadata& metadata) {
    if (metadata.type == "RenderMaterial") {
        const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext.ReadMaterialAsset(metadata.id);
        if (!material.has_value()) {
            return std::nullopt;
        }
        return MaterialEditorDocumentView{
            .material = *material,
            .assetKind = "Material",
            .parentMaterialAssetId = {},
        };
    }

    if (metadata.type != "RenderMaterialInstance") {
        return std::nullopt;
    }

    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const std::filesystem::path instancePath = ResolveAssetPath(manager, metadata);
    if (instancePath.empty()) {
        return std::nullopt;
    }
    const std::optional<kb::render::RenderMaterialInstanceAssetData> instance = kb::render::RenderMaterialInstanceAssetLoader::LoadInstance(instancePath);
    if (!instance.has_value() || !instance->parentMaterialAssetId.IsValid()) {
        return std::nullopt;
    }
    const std::optional<kb::render::RenderMaterialAssetData> parent = sceneContext.ReadMaterialAsset(instance->parentMaterialAssetId);
    if (!parent.has_value()) {
        return std::nullopt;
    }
    return MaterialEditorDocumentView{
        .material = *parent,
        .assetKind = "Material Instance",
        .parentMaterialAssetId = instance->parentMaterialAssetId,
    };
}

void DrawGraphOverlay(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
}

void DrawPreviewOverlay(HDC dc, const MaterialEditorPanelLayout& layout, const EditorMaterialPreviewTelemetry& telemetry) {
    DrawGraphOverlay(dc, layout.previewFrame, RGB(9, 10, 12), RGB(54, 58, 66));
    DrawText(dc, layout.previewFrame, telemetry.materialLoaded ? "Preview" : "Preview unavailable", RGB(86, 92, 100), 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawAssetBadge(
    HDC dc,
    const MaterialEditorPanelLayout& layout,
    const kb::assets::AssetMetadata& metadata,
    const MaterialEditorDocumentView& document) {
    DrawGraphOverlay(dc, layout.assetBadge, RGB(24, 26, 30), RGB(54, 58, 66));
    const std::string assetName = metadata.name.empty() ? metadata.virtualPath.filename().string() : metadata.name;
    DrawText(dc, RECT{ layout.assetBadge.left + 10, layout.assetBadge.top + 6, layout.assetBadge.right - 10, layout.assetBadge.top + 26 }, assetName.c_str(), RGB(232, 236, 240), 13, FW_SEMIBOLD);
    DrawText(dc, RECT{ layout.assetBadge.left + 10, layout.assetBadge.top + 28, layout.assetBadge.right - 10, layout.assetBadge.bottom - 6 }, document.assetKind.c_str(), RGB(126, 201, 143), 10);
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

    DrawGraphCanvas(dc, content, document->material.graph, sceneContext, metadata.id, sceneContext.SelectedMaterialGraphNodeId());
    DrawPreviewOverlay(dc, layout, telemetry);
    DrawAssetBadge(dc, layout, metadata, *document);
}

} // namespace

void MaterialEditorPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    static_cast<void>(theme);
    GdiDrawing::FillRectColor(dc, content, RGB(26, 28, 31));
    DrawHeader(dc, content, sceneContext.HasDirtyMaterialAssetEdit());

    const kb::assets::AssetId assetId = sceneContext.AssetBrowser().InspectorAsset();
    const kb::assets::AssetMetadata* metadata = assetId.IsValid()
        ? sceneContext.Scene().Assets().Manager().Registry().Find(assetId)
        : nullptr;

    if (metadata == nullptr || !IsMaterialDocument(*metadata)) {
        const RECT body{ content.left, content.top + kHeaderHeight, content.right, content.bottom };
        DrawText(dc, body, "Select a Material (.kbmat) or Material Instance (.kbmatinst) in Project Files to inspect it here.", RGB(86, 92, 100), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        return;
    }

    DrawMaterialContent(dc, content, sceneContext, *metadata);
}

std::optional<RECT> MaterialEditorPanelRenderer::MaterialPreviewRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    const kb::assets::AssetId assetId = sceneContext.AssetBrowser().InspectorAsset();
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
