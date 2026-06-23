#include "rendering/MaterialEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "inspection/EditorValueFormatter.hpp"
#include "inspection/InspectorMaterialTextureSlotFormatter.hpp"
#include "inspection/MaterialAssetFormatter.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/material_preview/EditorMaterialPreviewTelemetry.hpp"

#include <string>

namespace kb::editor {
namespace {

constexpr int kHeaderHeight = MaterialEditorPanelMetrics::HeaderHeight;
constexpr int kPadding = MaterialEditorPanelMetrics::Padding;
constexpr int kRowHeight = MaterialEditorPanelMetrics::RowHeight;
constexpr int kSectionHeight = MaterialEditorPanelMetrics::SectionHeight;
constexpr int kLabelWidth = 132;
constexpr int kCompactLabelWidth = 96;
constexpr int kTitleHeight = MaterialEditorPanelMetrics::TitleHeight;
constexpr int kPreviewGap = MaterialEditorPanelMetrics::PreviewGap;

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

void DrawSectionHeader(HDC dc, const RECT& content, int& y, const char* title) {
    const RECT bar{ content.left + kPadding, y, content.right - kPadding, y + kSectionHeight };
    GdiDrawing::DrawSharpFrame(dc, bar, RGB(28, 31, 35), RGB(13, 14, 16));
    DrawText(dc, RECT{ bar.left + 10, bar.top, bar.right - 10, bar.bottom }, title, RGB(210, 216, 222), 12, FW_SEMIBOLD);
    y += kSectionHeight + 4;
}

void DrawField(HDC dc, const RECT& content, int& y, const std::string& label, const std::string& value, COLORREF valueColor = RGB(196, 205, 214)) {
    const RECT row{ content.left + kPadding, y, content.right - kPadding, y + kRowHeight };
    DrawText(dc, RECT{ row.left, row.top, row.left + kLabelWidth, row.bottom }, label.c_str(), RGB(122, 130, 144), 12);
    DrawText(dc, RECT{ row.left + kLabelWidth, row.top, row.right, row.bottom }, value.c_str(), valueColor, 12);
    y += kRowHeight;
}

void DrawCompactField(HDC dc, const RECT& rect, const std::string& label, const std::string& value) {
    DrawText(dc, RECT{ rect.left, rect.top, rect.left + kCompactLabelWidth, rect.bottom }, label.c_str(), RGB(122, 130, 144), 11);
    DrawText(dc, RECT{ rect.left + kCompactLabelWidth, rect.top, rect.right, rect.bottom }, value.c_str(), RGB(196, 205, 214), 11);
}

void DrawParameterPair(
    HDC dc,
    const RECT& content,
    int& y,
    const std::string& leftLabel,
    const std::string& leftValue,
    const std::string& rightLabel,
    const std::string& rightValue) {
    const int gap = 12;
    const int innerLeft = content.left + kPadding;
    const int innerRight = content.right - kPadding;
    const int columnWidth = (innerRight - innerLeft - gap) / 2;
    const RECT left{ innerLeft, y, innerLeft + columnWidth, y + kRowHeight };
    const RECT right{ left.right + gap, y, innerRight, y + kRowHeight };
    DrawCompactField(dc, left, leftLabel, leftValue);
    DrawCompactField(dc, right, rightLabel, rightValue);
    y += kRowHeight;
}

void DrawTextureField(HDC dc, const RECT& content, int& y, const char* label, const kb::assets::AssetManager& manager, std::uint64_t textureAssetId) {
    const std::string display = InspectorMaterialTextureSlotFormatter::DisplayName(manager, textureAssetId);
    const bool missing = textureAssetId != 0U && InspectorMaterialTextureSlotFormatter::IsMissing(manager, textureAssetId);
    DrawField(dc, content, y, label, display, missing ? RGB(232, 112, 112) : RGB(196, 205, 214));
}

[[nodiscard]] std::string MarkExplicit(bool explicitFlag) {
    return explicitFlag ? std::string{} : std::string{ " (default)" };
}

[[nodiscard]] RECT PreviewFrameRect(const RECT& content) noexcept {
    return MaterialEditorPanelRenderer::ResolveLayout(content).previewFrame;
}

void DrawMaterialContent(HDC dc, const RECT& content, const EditorSceneContext& sceneContext, const kb::assets::AssetMetadata& metadata) {
    int y = content.top + kHeaderHeight + kPadding;

    const kb::render::RenderMaterialAssetData material = sceneContext.ReadMaterialAsset(metadata.id).value_or(kb::render::RenderMaterialAssetData{});
    const EditorMaterialPreviewTelemetry telemetry = sceneContext.MaterialPreviewTelemetry();

    // Live material preview (sphere) - the first visual signal. The bgfx viewport is
    // presented into this rect by the editor frame loop; the frame + label are the GDI
    // fallback shown until/unless the bgfx surface covers it.
    const RECT frame = PreviewFrameRect(content);
    GdiDrawing::DrawSharpFrame(dc, frame, RGB(13, 15, 18), RGB(45, 48, 54));
    DrawText(dc, frame, telemetry.materialLoaded ? "Material preview" : "Material fallback preview", RGB(86, 92, 100), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    y = frame.bottom + kPreviewGap;

    const std::string assetName = metadata.name.empty() ? metadata.virtualPath.filename().string() : metadata.name;
    DrawText(dc, RECT{ content.left + kPadding, y, content.right - kPadding, y + kTitleHeight }, assetName.c_str(), RGB(232, 236, 240), 15, FW_SEMIBOLD);
    y += kTitleHeight;
    DrawText(dc, RECT{ content.left + kPadding, y, content.right - kPadding, y + kRowHeight }, "Material Instance", RGB(126, 201, 143), 11);
    y += kRowHeight + 6;

    DrawSectionHeader(dc, content, y, "Parameters");
    const auto& desc = material.desc;
    const std::string baseColor = EditorValueFormatter::FormatFloat(desc.baseColor[0]) + ", "
        + EditorValueFormatter::FormatFloat(desc.baseColor[1]) + ", "
        + EditorValueFormatter::FormatFloat(desc.baseColor[2]) + ", "
        + EditorValueFormatter::FormatFloat(desc.baseColor[3]);
    const std::string emissiveColor = EditorValueFormatter::FormatFloat(desc.emissiveColor[0]) + ", "
        + EditorValueFormatter::FormatFloat(desc.emissiveColor[1]) + ", "
        + EditorValueFormatter::FormatFloat(desc.emissiveColor[2]);
    const std::string tiling = EditorValueFormatter::FormatFloat(desc.uvTiling[0]) + ", " + EditorValueFormatter::FormatFloat(desc.uvTiling[1]);
    const std::string offset = EditorValueFormatter::FormatFloat(desc.uvOffset[0]) + ", " + EditorValueFormatter::FormatFloat(desc.uvOffset[1]);

    DrawParameterPair(dc, content, y, "Base Color", baseColor, "Metallic", EditorValueFormatter::FormatFloat(desc.metallicFactor));
    DrawParameterPair(dc, content, y, "Roughness", EditorValueFormatter::FormatFloat(desc.roughnessFactor), "Normal Scale", EditorValueFormatter::FormatFloat(desc.normalScale));
    DrawParameterPair(dc, content, y, "Occlusion", EditorValueFormatter::FormatFloat(desc.occlusionStrength), "Emissive", emissiveColor);
    DrawParameterPair(dc, content, y, "Emissive Str.", EditorValueFormatter::FormatFloat(desc.emissiveStrength), "Alpha Mode", MaterialAssetFormatter::AlphaModeName(desc.alphaMode));
    DrawParameterPair(dc, content, y, "Alpha Cutoff", EditorValueFormatter::FormatFloat(desc.alphaCutoff), "Double Sided", desc.doubleSided ? "true" : "false");
    DrawParameterPair(dc, content, y, "Tiling", tiling, "Offset", offset);
    y += 6;

    DrawSectionHeader(dc, content, y, "Texture Slots");
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    DrawTextureField(dc, content, y, "Base Color", manager, desc.albedoTextureAssetId);
    DrawTextureField(dc, content, y, "Normal", manager, desc.normalTextureAssetId);
    DrawTextureField(dc, content, y, "Metallic-Roughness", manager, desc.metallicRoughnessTextureAssetId);
    DrawTextureField(dc, content, y, "Emissive", manager, desc.emissiveTextureAssetId);
    DrawTextureField(dc, content, y, "Occlusion", manager, desc.occlusionTextureAssetId);
    y += 6;

    DrawSectionHeader(dc, content, y, "Preview Status");
    DrawField(dc, content, y, "Preview Scene", telemetry.previewSceneReady ? "Ready" : "Fallback");
    DrawField(dc, content, y, "Cache", telemetry.materialLoaded ? "Loaded" : "Missing");
    DrawField(dc, content, y, "Missing Textures", EditorValueFormatter::FormatUInt64(telemetry.missingTextureCount));
    y += 6;

    DrawSectionHeader(dc, content, y, "Identity");
    DrawField(dc, content, y, "Asset Id", EditorValueFormatter::FormatUInt64(metadata.id.value));
    DrawField(dc, content, y, "Virtual Path", metadata.virtualPath.generic_string());
    DrawField(dc, content, y, "Material Type", material.materialType + MarkExplicit(material.hasExplicitMaterialType));
    DrawField(dc, content, y, "Type Version", EditorValueFormatter::FormatUInt64(material.materialTypeVersion) + MarkExplicit(material.hasExplicitMaterialTypeVersion));
    DrawField(dc, content, y, "Document Version", EditorValueFormatter::FormatUInt64(material.documentVersion) + MarkExplicit(material.hasExplicitDocumentVersion));
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

    if (metadata == nullptr || metadata->type != "RenderMaterial") {
        const RECT body{ content.left, content.top + kHeaderHeight, content.right, content.bottom };
        DrawText(dc, body, "Select a Material Instance (.kbmat) in Project Files to inspect it here.", RGB(86, 92, 100), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
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
    if (metadata == nullptr || metadata->type != "RenderMaterial") {
        return std::nullopt;
    }
    const RECT frame = PreviewFrameRect(content);
    return RECT{ frame.left + 1, frame.top + 1, frame.right - 1, frame.bottom - 1 };
}

} // namespace kb::editor

#endif
