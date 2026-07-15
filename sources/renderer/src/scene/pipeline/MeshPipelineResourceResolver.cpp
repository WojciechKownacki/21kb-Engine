#include "scene/pipeline/MeshPipelineResourceResolver.hpp"

#include "kb/render/resources/RenderMaterialTextureSlots.hpp"
#include "scene/pipeline/MeshPipelinePassPolicy.hpp"

namespace kb::render {
namespace {

[[nodiscard]] const RenderMaterialResource& ErrorMaterial() noexcept {
    static const RenderMaterialResource material{
        .baseColor = { 1.0F, 0.0F, 1.0F, 1.0F },
    };
    return material;
}

[[nodiscard]] bool IsSceneMeshVertexFormatSupported(RenderVertexFormat format) noexcept {
    switch (format) {
    case RenderVertexFormat::P3N3UV2:
    case RenderVertexFormat::P3N3T4UV2:
        return true;
    case RenderVertexFormat::P3C3:
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4:
        return false;
    }
    return false;
}

void EmitPassDiagnostics(
    SceneRenderDiagnostics* diagnostics,
    SceneRenderDiagnosticKind kind,
    SceneRenderDiagnosticSeverity severity,
    MeshPassType pass,
    const SceneMeshBatch& batch,
    std::uint64_t materialAssetId,
    std::span<const std::uint64_t> selectedEntityIds,
    std::uint32_t cullingMask) {
    if (diagnostics == nullptr) {
        return;
    }

    for (const SceneRenderMeshInstance& instance : batch.instances) {
        if (!MeshPipelinePassPolicy::CanEverContain(pass, instance, selectedEntityIds, cullingMask)) {
            continue;
        }
        diagnostics->events.push_back(SceneRenderDiagnosticEvent{
            .severity = severity,
            .kind = kind,
            .entityId = instance.entityId,
            .meshAssetId = batch.meshAssetId,
            .materialAssetId = materialAssetId,
            .instanceCount = 1U,
        });
    }
}

void EmitInstanceDiagnostic(
    SceneRenderDiagnostics* diagnostics,
    SceneRenderDiagnosticKind kind,
    SceneRenderDiagnosticSeverity severity,
    const SceneRenderMeshInstance& instance,
    std::uint64_t materialAssetId) {
    if (diagnostics == nullptr) {
        return;
    }

    diagnostics->events.push_back(SceneRenderDiagnosticEvent{
        .severity = severity,
        .kind = kind,
        .entityId = instance.entityId,
        .meshAssetId = instance.meshAssetId,
        .materialAssetId = materialAssetId,
        .instanceCount = 1U,
    });
}

void EmitTextureDimensionMismatchDiagnostic(
    SceneRenderDiagnostics* diagnostics,
    const SceneRenderMeshInstance& instance,
    std::uint64_t materialAssetId,
    std::uint64_t textureAssetId,
    RenderTextureDimension expected,
    RenderTextureDimension actual) {
    if (diagnostics == nullptr) {
        return;
    }

    diagnostics->events.push_back(SceneRenderDiagnosticEvent{
        .severity = SceneRenderDiagnosticSeverity::Error,
        .kind = SceneRenderDiagnosticKind::TextureDimensionMismatch,
        .entityId = instance.entityId,
        .meshAssetId = instance.meshAssetId,
        .materialAssetId = materialAssetId,
        .textureAssetId = textureAssetId,
        .instanceCount = 1U,
        .expectedTextureDimension = expected,
        .actualTextureDimension = actual,
        .fallbackTextureDimension = expected,
    });
}

} // namespace

MeshPipelineResolvedMesh MeshPipelineResourceResolver::ResolveMeshBatch(
    MeshPassType pass,
    const SceneMeshBatch& batch,
    std::uint32_t passInstanceCount,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    SceneRenderSubmitStats& stats,
    SceneRenderDiagnostics* diagnostics,
    std::span<const std::uint64_t> selectedEntityIds,
    std::uint32_t cullingMask) noexcept {
    const RenderMeshHandle meshHandle = resourceMap.ResolveMesh(batch.meshAssetId);
    if (!meshHandle.IsValid()) {
        stats.visibleMeshCount += passInstanceCount;
        ++stats.visibleDrawGroupCount;
        stats.missingMeshBindingCount += passInstanceCount;
        EmitPassDiagnostics(diagnostics, SceneRenderDiagnosticKind::MissingMeshBinding, SceneRenderDiagnosticSeverity::Error, pass, batch, batch.materialAssetId, selectedEntityIds, cullingMask);
        return {};
    }

    const RenderMeshResource* meshResource = resources.FindMesh(meshHandle);
    if (meshResource == nullptr) {
        stats.visibleMeshCount += passInstanceCount;
        ++stats.visibleDrawGroupCount;
        stats.missingMeshResourceCount += passInstanceCount;
        EmitPassDiagnostics(diagnostics, SceneRenderDiagnosticKind::MissingMeshResource, SceneRenderDiagnosticSeverity::Error, pass, batch, batch.materialAssetId, selectedEntityIds, cullingMask);
        return {};
    }
    if (!IsSceneMeshVertexFormatSupported(meshResource->vertexFormat)) {
        stats.visibleMeshCount += passInstanceCount;
        ++stats.visibleDrawGroupCount;
        stats.unsupportedMeshVertexFormatCount += passInstanceCount;
        EmitPassDiagnostics(diagnostics, SceneRenderDiagnosticKind::UnsupportedMeshVertexFormat, SceneRenderDiagnosticSeverity::Error, pass, batch, batch.materialAssetId, selectedEntityIds, cullingMask);
        return {};
    }
    if (!bgfx::isValid(meshResource->vertexBuffer) || !bgfx::isValid(meshResource->indexBuffer)) {
        stats.visibleMeshCount += passInstanceCount;
        ++stats.visibleDrawGroupCount;
        stats.missingMeshResourceCount += passInstanceCount;
        EmitPassDiagnostics(diagnostics, SceneRenderDiagnosticKind::MissingMeshResource, SceneRenderDiagnosticSeverity::Error, pass, batch, batch.materialAssetId, selectedEntityIds, cullingMask);
        return {};
    }

    return MeshPipelineResolvedMesh{
        .handle = meshHandle,
        .resource = meshResource,
    };
}

const RenderMaterialResource* MeshPipelineResourceResolver::ResolveMaterialOrFallback(
    const SceneRenderMeshInstance& instance,
    std::uint64_t materialAssetId,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    RenderMaterialHandle& outHandle,
    SceneRenderSubmitStats& stats,
    SceneRenderDiagnostics* diagnostics) noexcept {
    outHandle = {};
    if (materialAssetId == 0U) {
        return nullptr;
    }

    outHandle = resourceMap.ResolveMaterial(materialAssetId);
    if (!outHandle.IsValid()) {
        ++stats.missingMaterialBindingCount;
        EmitInstanceDiagnostic(diagnostics, SceneRenderDiagnosticKind::MissingMaterialBinding, SceneRenderDiagnosticSeverity::Error, instance, materialAssetId);
        return &ErrorMaterial();
    }

    const RenderMaterialResource* materialResource = resources.FindMaterial(outHandle);
    if (materialResource == nullptr) {
        ++stats.missingMaterialResourceCount;
        EmitInstanceDiagnostic(diagnostics, SceneRenderDiagnosticKind::MissingMaterialResource, SceneRenderDiagnosticSeverity::Error, instance, materialAssetId);
        return &ErrorMaterial();
    }

    return materialResource;
}

void MeshPipelineResourceResolver::ValidateMaterialTextureOrFallback(
    const SceneRenderMeshInstance& instance,
    std::uint64_t materialAssetId,
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    SceneRenderSubmitStats& stats,
    SceneRenderDiagnostics* diagnostics) noexcept {
    if (material == nullptr) {
        return;
    }

    auto validateTexture = [&](RenderTextureHandle directHandle,
                               std::uint64_t textureAssetId,
                               RenderTextureColorSpace colorSpace,
                               RenderTextureDimension expectedDimension) {
        const RenderTextureResource* texture = nullptr;
        if (directHandle.IsValid()) {
            texture = resources.FindTexture(directHandle);
            if (texture == nullptr) {
                ++stats.missingTextureResourceCount;
                EmitInstanceDiagnostic(diagnostics, SceneRenderDiagnosticKind::MissingTextureResource, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
            }
        } else {
            if (textureAssetId == 0U) {
                return;
            }

            const RenderTextureHandle textureHandle = resourceMap.ResolveTexture(textureAssetId, colorSpace);
            if (!textureHandle.IsValid()) {
                ++stats.missingTextureBindingCount;
                EmitInstanceDiagnostic(diagnostics, SceneRenderDiagnosticKind::MissingTextureBinding, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
                return;
            }

            texture = resources.FindTexture(textureHandle);
            if (texture == nullptr) {
                ++stats.missingTextureResourceCount;
                EmitInstanceDiagnostic(diagnostics, SceneRenderDiagnosticKind::MissingTextureResource, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
            }
        }

        if (texture != nullptr && texture->dimension != expectedDimension) {
            ++stats.textureDimensionMismatchCount;
            EmitTextureDimensionMismatchDiagnostic(
                diagnostics,
                instance,
                materialAssetId,
                textureAssetId,
                expectedDimension,
                texture->dimension);
        }
    };

    for (const RenderMaterialTextureSlotBinding slot : RenderMaterialTextureSlots(*material)) {
        if (slot.policy.runtimeSupport == RenderMaterialFeatureSupport::Supported) {
            validateTexture(
                slot.directHandle,
                slot.assetId,
                RenderTextureBindingColorSpace(slot.policy.expectedColorSpace),
                RenderTextureDimension::Texture2D);
        }
    }
    for (const RenderMaterialGraphTextureBinding& graphTexture : material->graphProgram.textures) {
        validateTexture(
            graphTexture.texture,
            graphTexture.textureAssetId,
            graphTexture.colorSpace,
            graphTexture.dimension);
    }
}

std::uint64_t MeshPipelineResourceResolver::MaterialAssetForSectionInstance(
    const SceneMeshBatch& batch,
    const SceneRenderMeshInstance& instance,
    const RenderMeshResource* meshResource,
    const RenderMeshSection& section) noexcept {
    if (section.materialSlot < instance.materialSlotOverrideCount &&
        section.materialSlot < kMaxSceneMaterialSlotOverrides &&
        instance.materialSlotAssetIds[section.materialSlot] != 0U) {
        return instance.materialSlotAssetIds[section.materialSlot];
    }
    if (batch.materialAssetId != 0U) {
        return batch.materialAssetId;
    }
    if (meshResource != nullptr && section.materialSlot < meshResource->materialSlots.size()) {
        const std::uint64_t defaultMaterialAssetId = meshResource->materialSlots[section.materialSlot].defaultMaterialAssetId;
        if (defaultMaterialAssetId != 0U) {
            return defaultMaterialAssetId;
        }
    }
    return 0U;
}

} // namespace kb::render
