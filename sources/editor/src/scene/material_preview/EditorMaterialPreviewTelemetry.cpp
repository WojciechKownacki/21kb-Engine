#include "scene/material_preview/EditorMaterialPreviewTelemetry.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

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

    telemetry.graphBacked = kb::render::HasGraphAuthoringData(material->graph);
    if (telemetry.graphBacked) {
        const kb::render::RenderMaterialGraphCompileResult compile = kb::render::CompileRenderMaterialGraphToShaderSource(
            material->graph,
            kb::render::RenderMaterialGraphBuildContext{ .assetId = materialAssetId.value });
        telemetry.graphProgramKey = compile.shader.sourceHash;
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : compile.diagnostics) {
            telemetry.compileDiagnostics.push_back(
                std::string{ kb::render::RenderMaterialGraphDiagnosticSeverityName(diagnostic.severity) } + " " +
                std::string{ kb::render::RenderMaterialGraphDiagnosticKindName(diagnostic.kind) } +
                (diagnostic.message.empty() ? std::string{} : ": " + diagnostic.message));
        }
        telemetry.graphRuntimeState = kb::render::ResolveRenderMaterialGraphRuntimeState(kb::render::RenderMaterialGraphRuntimeStateInput{
            .phase = kb::render::RenderMaterialGraphCompilePhase::Compiled,
            .validationSucceeded = compile.Succeeded(),
            .compileSucceeded = compile.Succeeded(),
            .hasGpuProgram = compile.Succeeded(),
            .hasLastGood = material->graph.lastGoodArtifact.IsValid(),
            .fallbackApplied = true,
            .failurePolicy = material->graph.artifactFailurePolicy,
        });
        switch (telemetry.graphRuntimeState) {
        case kb::render::RenderMaterialGraphRuntimeState::UsingGpuGraph:
            telemetry.renderMode = MaterialPreviewRenderMode::GpuMaterialGraph;
            break;
        case kb::render::RenderMaterialGraphRuntimeState::UsingLastGood:
            telemetry.renderMode = MaterialPreviewRenderMode::LastGood;
            break;
        case kb::render::RenderMaterialGraphRuntimeState::UsingErrorMaterial:
            telemetry.renderMode = MaterialPreviewRenderMode::ErrorMaterial;
            break;
        default:
            telemetry.renderMode = MaterialPreviewRenderMode::CpuPbrFlatteningFallback;
            break;
        }
    } else {
        telemetry.renderMode = MaterialPreviewRenderMode::BuiltinPbr;
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
