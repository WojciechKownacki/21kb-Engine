#include "scene/material_preview/EditorMaterialPreviewTelemetry.hpp"

#include "engine/assets/AssetMetadata.hpp"

#include <array>

namespace kb::editor {
namespace {

struct TextureSlotDiagnostic {
    const char* label = "";
    std::uint64_t assetId = 0U;
};

void AppendMissingTexture(
    const kb::assets::AssetManager& manager,
    const TextureSlotDiagnostic& slot,
    EditorMaterialPreviewTelemetry& telemetry) {
    if (slot.assetId == 0U) {
        return;
    }
    if (manager.Registry().Find(kb::assets::AssetId{slot.assetId}) != nullptr) {
        return;
    }
    ++telemetry.missingTextureCount;
    telemetry.missingTextures.push_back(std::string{slot.label} + " texture missing asset " + std::to_string(slot.assetId));
}

} // namespace

EditorMaterialPreviewTelemetry EditorMaterialPreviewTelemetryBuilder::Build(
    const kb::assets::AssetManager& manager,
    kb::assets::AssetId materialAssetId,
    const kb::render::RenderMaterialAssetData* material,
    bool previewSceneReady) {
    EditorMaterialPreviewTelemetry telemetry{
        .materialAssetId = materialAssetId,
        .materialMetadataFound = manager.Registry().Find(materialAssetId) != nullptr,
        .materialLoaded = material != nullptr,
        .previewSceneReady = previewSceneReady,
    };
    if (material == nullptr) {
        return telemetry;
    }

    const std::array<TextureSlotDiagnostic, 5U> slots{{
        {"Albedo", material->desc.albedoTextureAssetId},
        {"Normal", material->desc.normalTextureAssetId},
        {"Metallic-Roughness", material->desc.metallicRoughnessTextureAssetId},
        {"Occlusion", material->desc.occlusionTextureAssetId},
        {"Emissive", material->desc.emissiveTextureAssetId},
    }};
    for (const TextureSlotDiagnostic& slot : slots) {
        AppendMissingTexture(manager, slot, telemetry);
    }
    return telemetry;
}

} // namespace kb::editor
