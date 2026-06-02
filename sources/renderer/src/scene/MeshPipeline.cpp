#include "kb/render/scene/MeshPipeline.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/frame/RenderPassKind.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] const RenderMaterialResource& ErrorMaterial() noexcept {
    static const RenderMaterialResource material{
        .baseColor = { 1.0F, 0.0F, 1.0F, 1.0F },
    };
    return material;
}

[[nodiscard]] bool IsTransparent(const RenderMaterialResource* material) noexcept {
    return material != nullptr && material->alphaMode == RenderMaterialAlphaMode::Blend;
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

struct FrustumPlane {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

struct Frustum {
    std::array<FrustumPlane, 6> planes{};
    bool valid = false;
};

[[nodiscard]] float Length3(float x, float y, float z) noexcept {
    return std::sqrt(x * x + y * y + z * z);
}

[[nodiscard]] FrustumPlane NormalizePlane(FrustumPlane plane) noexcept {
    const float length = Length3(plane.x, plane.y, plane.z);
    if (length <= 0.00001F) {
        return {};
    }
    const float invLength = 1.0F / length;
    return FrustumPlane{
        .x = plane.x * invLength,
        .y = plane.y * invLength,
        .z = plane.z * invLength,
        .w = plane.w * invLength,
    };
}

[[nodiscard]] std::array<float, 16> MultiplyColumnMajor(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs) noexcept {
    std::array<float, 16> result{};
    for (std::uint32_t column = 0U; column < 4U; ++column) {
        for (std::uint32_t row = 0U; row < 4U; ++row) {
            float value = 0.0F;
            for (std::uint32_t k = 0U; k < 4U; ++k) {
                value += lhs[k * 4U + row] * rhs[column * 4U + k];
            }
            result[column * 4U + row] = value;
        }
    }
    return result;
}

[[nodiscard]] Frustum BuildFrustum(const SceneRenderCamera* camera) noexcept {
    if (camera == nullptr) {
        return {};
    }

    const std::array<float, 16> clip = MultiplyColumnMajor(camera->projection, camera->view);
    const std::array<float, 4> row0{ clip[0], clip[4], clip[8], clip[12] };
    const std::array<float, 4> row1{ clip[1], clip[5], clip[9], clip[13] };
    const std::array<float, 4> row2{ clip[2], clip[6], clip[10], clip[14] };
    const std::array<float, 4> row3{ clip[3], clip[7], clip[11], clip[15] };

    return Frustum{
        .planes = {
            NormalizePlane(FrustumPlane{ row3[0] + row0[0], row3[1] + row0[1], row3[2] + row0[2], row3[3] + row0[3] }),
            NormalizePlane(FrustumPlane{ row3[0] - row0[0], row3[1] - row0[1], row3[2] - row0[2], row3[3] - row0[3] }),
            NormalizePlane(FrustumPlane{ row3[0] + row1[0], row3[1] + row1[1], row3[2] + row1[2], row3[3] + row1[3] }),
            NormalizePlane(FrustumPlane{ row3[0] - row1[0], row3[1] - row1[1], row3[2] - row1[2], row3[3] - row1[3] }),
            NormalizePlane(FrustumPlane{ row3[0] + row2[0], row3[1] + row2[1], row3[2] + row2[2], row3[3] + row2[3] }),
            NormalizePlane(FrustumPlane{ row3[0] - row2[0], row3[1] - row2[1], row3[2] - row2[2], row3[3] - row2[3] }),
        },
        .valid = true,
    };
}

[[nodiscard]] bool IsInsideFrustum(const Frustum& frustum, const RenderBoundsSphere& bounds) noexcept {
    if (!frustum.valid || !bounds.IsValid()) {
        return true;
    }

    for (const FrustumPlane& plane : frustum.planes) {
        const float distance = plane.x * bounds.center[0] + plane.y * bounds.center[1] + plane.z * bounds.center[2] + plane.w;
        if (distance < -bounds.radius) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] float MaxAxisScale(const std::array<float, 16>& model) noexcept {
    const float scaleX = Length3(model[0], model[1], model[2]);
    const float scaleY = Length3(model[4], model[5], model[6]);
    const float scaleZ = Length3(model[8], model[9], model[10]);
    return std::max(scaleX, std::max(scaleY, scaleZ));
}

[[nodiscard]] RenderBoundsSphere TransformBounds(const RenderBoundsSphere& localBounds, const std::array<float, 16>& model) noexcept {
    if (!localBounds.IsValid()) {
        return {};
    }

    const float x = localBounds.center[0];
    const float y = localBounds.center[1];
    const float z = localBounds.center[2];
    return RenderBoundsSphere{
        .center = {
            model[0] * x + model[4] * y + model[8] * z + model[12],
            model[1] * x + model[5] * y + model[9] * z + model[13],
            model[2] * x + model[6] * y + model[10] * z + model[14],
        },
        .radius = localBounds.radius * MaxAxisScale(model),
    };
}

[[nodiscard]] float ViewDepth(const SceneRenderCamera* camera, const RenderBoundsSphere& bounds) noexcept {
    if (camera == nullptr || !bounds.IsValid()) {
        return 0.0F;
    }
    const std::array<float, 16>& view = camera->view;
    return view[2] * bounds.center[0] + view[6] * bounds.center[1] + view[10] * bounds.center[2] + view[14];
}

[[nodiscard]] std::uint16_t DepthBucket(float depth) noexcept {
    const float shifted = std::clamp((std::abs(depth) * 16.0F), 0.0F, 65535.0F);
    return static_cast<std::uint16_t>(shifted);
}

[[nodiscard]] std::uint32_t ResourceKey20(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value & 0xFFFFFU);
}

[[nodiscard]] std::uint16_t SortDepthBucket(MeshPassType pass, std::uint16_t depthBucket) noexcept {
    return pass == MeshPassType::BaseTransparent
        ? static_cast<std::uint16_t>(UINT16_MAX - depthBucket)
        : depthBucket;
}

[[nodiscard]] std::uint64_t BuildSortKey(MeshPassType pass, RenderMaterialHandle material, std::uint64_t materialAssetId, RenderMeshHandle mesh, std::uint64_t meshAssetId, std::uint16_t depthBucket) noexcept {
    const std::uint64_t passKey = static_cast<std::uint64_t>(static_cast<std::uint8_t>(pass) & 0x0FU);
    const std::uint64_t materialKey = ResourceKey20(material.IsValid() ? material.value : materialAssetId);
    const std::uint64_t meshKey = ResourceKey20(mesh.IsValid() ? mesh.value : meshAssetId);
    return (passKey << 60U) | (materialKey << 40U) | (meshKey << 20U) | static_cast<std::uint64_t>(SortDepthBucket(pass, depthBucket));
}

[[nodiscard]] bool IsSelectedEntity(std::span<const std::uint64_t> selectedEntityIds, std::uint64_t entityId) noexcept {
    return std::ranges::find(selectedEntityIds, entityId) != selectedEntityIds.end();
}

[[nodiscard]] bool InstanceCanEverBelongToPass(MeshPassType pass, const SceneRenderMeshInstance& instance, std::span<const std::uint64_t> selectedEntityIds) noexcept {
    switch (pass) {
    case MeshPassType::ShadowDepth:
        return instance.castsShadow;
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
        return IsSelectedEntity(selectedEntityIds, instance.entityId);
    case MeshPassType::Depth:
    case MeshPassType::BaseOpaque:
    case MeshPassType::BaseTransparent:
    case MeshPassType::Gizmo:
        return true;
    }

    return true;
}

[[nodiscard]] std::uint32_t CountPassInstances(MeshPassType pass, const SceneRenderDrawGroup& group, std::span<const std::uint64_t> selectedEntityIds) noexcept {
    std::uint32_t count = 0U;
    for (const SceneRenderMeshInstance& instance : group.instances) {
        count += InstanceCanEverBelongToPass(pass, instance, selectedEntityIds) ? 1U : 0U;
    }
    return count;
}

void EmitPassDiagnostics(
    SceneRenderDiagnostics* diagnostics,
    SceneRenderDiagnosticKind kind,
    SceneRenderDiagnosticSeverity severity,
    MeshPassType pass,
    const SceneRenderDrawGroup& group,
    std::uint64_t materialAssetId,
    std::span<const std::uint64_t> selectedEntityIds) {
    if (diagnostics == nullptr) {
        return;
    }

    for (const SceneRenderMeshInstance& instance : group.instances) {
        if (!InstanceCanEverBelongToPass(pass, instance, selectedEntityIds)) {
            continue;
        }
        diagnostics->events.push_back(SceneRenderDiagnosticEvent{
            .severity = severity,
            .kind = kind,
            .entityId = instance.entityId,
            .meshAssetId = group.meshAssetId,
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

[[nodiscard]] bool InstanceBelongsToPass(MeshPassType pass, const SceneRenderMeshInstance& instance, const RenderMaterialResource* material, std::span<const std::uint64_t> selectedEntityIds) noexcept {
    switch (pass) {
    case MeshPassType::Depth:
        return !IsTransparent(material);
    case MeshPassType::BaseOpaque:
        return !IsTransparent(material);
    case MeshPassType::BaseTransparent:
        return IsTransparent(material);
    case MeshPassType::ShadowDepth:
        return instance.castsShadow && !IsTransparent(material);
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
        return IsSelectedEntity(selectedEntityIds, instance.entityId);
    case MeshPassType::Gizmo:
        return true;
    }

    return false;
}

[[nodiscard]] std::uint64_t MeshPassState(MeshPassType pass, const RenderMeshResource* mesh, const RenderMaterialResource* material) noexcept {
    const bool doubleSided = (mesh != nullptr && mesh->doubleSided) || (material != nullptr && material->doubleSided);
    const std::uint64_t rasterStateExtra = mesh == nullptr ? 0U : mesh->rasterStateExtra;
    const std::uint64_t cullState = doubleSided ? 0U : BGFX_STATE_CULL_CW;

    switch (pass) {
    case MeshPassType::Depth:
    case MeshPassType::ShadowDepth:
        return BGFX_STATE_WRITE_Z | SceneDepthPolicy::DepthTestState() | cullState | rasterStateExtra;
    case MeshPassType::BaseTransparent:
        return SceneDepthPolicy::SceneDepthReadState() | BGFX_STATE_BLEND_ALPHA | cullState | rasterStateExtra;
    case MeshPassType::SelectionId:
        return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | cullState | rasterStateExtra;
    case MeshPassType::EditorSelection:
    case MeshPassType::Gizmo:
        return SceneDepthPolicy::SceneOverlayState(true) | cullState | rasterStateExtra;
    case MeshPassType::BaseOpaque:
        return SceneDepthPolicy::SceneMeshState(doubleSided) | rasterStateExtra;
    }

    return SceneDepthPolicy::SceneMeshState(doubleSided) | rasterStateExtra;
}

[[nodiscard]] const RenderMaterialResource* ResolveMaterialOrFallback(
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

void ValidateMaterialTextureOrFallback(
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

    auto validateTexture = [&](RenderTextureHandle directHandle, std::uint64_t textureAssetId) {
        if (directHandle.IsValid()) {
            if (resources.FindTexture(directHandle) == nullptr) {
                ++stats.missingTextureResourceCount;
                EmitInstanceDiagnostic(diagnostics, SceneRenderDiagnosticKind::MissingTextureResource, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
            }
            return;
        }

        if (textureAssetId == 0U) {
            return;
        }

        const RenderTextureHandle textureHandle = resourceMap.ResolveTexture(textureAssetId);
        if (!textureHandle.IsValid()) {
            ++stats.missingTextureBindingCount;
            EmitInstanceDiagnostic(diagnostics, SceneRenderDiagnosticKind::MissingTextureBinding, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
            return;
        }

        if (resources.FindTexture(textureHandle) == nullptr) {
            ++stats.missingTextureResourceCount;
            EmitInstanceDiagnostic(diagnostics, SceneRenderDiagnosticKind::MissingTextureResource, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
        }
    };

    validateTexture(material->albedoTexture, material->albedoTextureAssetId);
    validateTexture(material->normalTexture, material->normalTextureAssetId);
    validateTexture(material->metallicRoughnessTexture, material->metallicRoughnessTextureAssetId);
    validateTexture(material->emissiveTexture, material->emissiveTextureAssetId);
}

[[nodiscard]] std::uint64_t MaterialAssetForSectionInstance(const SceneRenderDrawGroup& group, const SceneRenderMeshInstance& instance, const RenderMeshResource* meshResource, const RenderMeshSection& section) noexcept {
    if (section.materialSlot < instance.materialSlotOverrideCount &&
        section.materialSlot < kMaxSceneMaterialSlotOverrides &&
        instance.materialSlotAssetIds[section.materialSlot] != 0U) {
        return instance.materialSlotAssetIds[section.materialSlot];
    }
    if (group.materialAssetId != 0U) {
        return group.materialAssetId;
    }
    if (meshResource == nullptr || section.materialSlot >= meshResource->materialSlots.size()) {
        return 0U;
    }
    return meshResource->materialSlots[section.materialSlot].defaultMaterialAssetId;
}

void ResetCommandKeepingInstanceStorage(MeshDrawCommand& command) noexcept {
    command.pass = MeshPassType::BaseOpaque;
    command.meshAssetId = 0U;
    command.materialAssetId = 0U;
    command.sectionIndex = 0U;
    command.materialSlot = 0U;
    command.indexStart = 0U;
    command.indexCount = 0U;
    command.depthBucket = 0U;
    command.mesh = {};
    command.material = {};
    command.meshResource = nullptr;
    command.materialResource = nullptr;
    command.state = 0U;
    command.sortKey = 0U;
    command.instances.clear();
}

[[nodiscard]] MeshDrawCommand& WritableCommand(MeshPipelineBuildResult& result, std::size_t index) {
    if (index == result.commands.size()) {
        result.commands.push_back(MeshDrawCommand{});
    }
    MeshDrawCommand& command = result.commands[index];
    ResetCommandKeepingInstanceStorage(command);
    return command;
}

} // namespace

const char* MeshPassTypeName(MeshPassType pass) noexcept {
    switch (pass) {
    case MeshPassType::Depth:
        return "Depth";
    case MeshPassType::BaseOpaque:
        return "BaseOpaque";
    case MeshPassType::BaseTransparent:
        return "BaseTransparent";
    case MeshPassType::ShadowDepth:
        return "ShadowDepth";
    case MeshPassType::SelectionId:
        return "SelectionId";
    case MeshPassType::EditorSelection:
        return "EditorSelection";
    case MeshPassType::Gizmo:
        return "Gizmo";
    }

    return "Unknown";
}

std::optional<MeshPassType> MeshPassForRenderPassKind(RenderPassKind kind) noexcept {
    switch (kind) {
    case RenderPassKind::ShadowDepth:
        return MeshPassType::ShadowDepth;
    case RenderPassKind::OpaqueScene:
        return MeshPassType::BaseOpaque;
    case RenderPassKind::TransparentScene:
        return MeshPassType::BaseTransparent;
    case RenderPassKind::EditorSelectionMask:
        return MeshPassType::SelectionId;
    case RenderPassKind::SceneTargetSetup:
    case RenderPassKind::PostProcessBloomPrefilter:
    case RenderPassKind::PostProcessBloomBlurH:
    case RenderPassKind::PostProcessBloomBlurV:
    case RenderPassKind::PostProcessHdrCombine:
    case RenderPassKind::PostProcessHdrFinalize:
    case RenderPassKind::EditorSceneOverlays:
    case RenderPassKind::FinalComposite:
    case RenderPassKind::EditorUiComposite:
        return std::nullopt;
    }

    return std::nullopt;
}

std::size_t MeshCommandLookupKeyHash::operator()(MeshCommandLookupKey key) const noexcept {
    const std::uint64_t mixed = key.materialAssetId ^ (key.materialHandleValue + 0x9e3779b97f4a7c15ULL + (key.materialAssetId << 6U) + (key.materialAssetId >> 2U));
    return static_cast<std::size_t>(mixed);
}

MeshPipelineBuildResult MeshPipelineProcessor::Build(const MeshPipelineBuildDesc& desc) noexcept {
    MeshPipelineBuildResult result{};
    BuildInto(desc, result);
    return result;
}

void MeshPipelineProcessor::BuildInto(const MeshPipelineBuildDesc& desc, MeshPipelineBuildResult& result) noexcept {
    for (MeshDrawCommand& command : result.commands) {
        command.instances.clear();
    }
    result.stats = SceneRenderSubmitStats{};
    if (desc.drawGroups == nullptr) {
        result.commands.clear();
        return;
    }

    const bool validateResources = desc.resourceValidation == MeshPipelineResourceValidation::ResolveAndValidate;
    if (validateResources && (desc.resources == nullptr || desc.resourceMap == nullptr)) {
        result.commands.clear();
        return;
    }

    const Frustum frustum = BuildFrustum(desc.camera);
    result.commands.reserve(desc.drawGroups->size());
    std::size_t writeCommandCount = 0U;
    std::uint32_t acceptedInstanceCount = 0U;
    for (const SceneRenderDrawGroup& group : *desc.drawGroups) {
        const std::uint32_t instanceCount = CountPassInstances(desc.pass, group, desc.selectedEntityIds);
        if (instanceCount == 0U) {
            continue;
        }

        RenderMeshHandle meshHandle{};
        const RenderMeshResource* meshResource = nullptr;
        if (validateResources) {
            meshHandle = desc.resourceMap->ResolveMesh(group.meshAssetId);
            if (!meshHandle.IsValid()) {
                result.stats.visibleMeshCount += instanceCount;
                ++result.stats.visibleDrawGroupCount;
                result.stats.missingMeshBindingCount += instanceCount;
                EmitPassDiagnostics(desc.diagnostics, SceneRenderDiagnosticKind::MissingMeshBinding, SceneRenderDiagnosticSeverity::Error, desc.pass, group, group.materialAssetId, desc.selectedEntityIds);
                continue;
            }

            meshResource = desc.resources->FindMesh(meshHandle);
            if (meshResource == nullptr) {
                result.stats.visibleMeshCount += instanceCount;
                ++result.stats.visibleDrawGroupCount;
                result.stats.missingMeshResourceCount += instanceCount;
                EmitPassDiagnostics(desc.diagnostics, SceneRenderDiagnosticKind::MissingMeshResource, SceneRenderDiagnosticSeverity::Error, desc.pass, group, group.materialAssetId, desc.selectedEntityIds);
                continue;
            }
            if (!IsSceneMeshVertexFormatSupported(meshResource->vertexFormat)) {
                result.stats.visibleMeshCount += instanceCount;
                ++result.stats.visibleDrawGroupCount;
                result.stats.unsupportedMeshVertexFormatCount += instanceCount;
                EmitPassDiagnostics(desc.diagnostics, SceneRenderDiagnosticKind::UnsupportedMeshVertexFormat, SceneRenderDiagnosticSeverity::Error, desc.pass, group, group.materialAssetId, desc.selectedEntityIds);
                continue;
            }
        }
        if (!validateResources) {
            meshResource = desc.resolvedMeshResource;
        }

        const RenderMeshSection fallbackSection{
            .indexStart = 0U,
            .indexCount = meshResource == nullptr ? 0U : meshResource->indexCount,
            .materialSlot = 0U,
            .bounds = meshResource == nullptr ? RenderBoundsSphere{} : meshResource->bounds,
        };
        const std::vector<RenderMeshSection>* sections = meshResource == nullptr ? nullptr : &meshResource->sections;
        const std::uint32_t sectionCount = sections == nullptr || sections->empty() ? 1U : static_cast<std::uint32_t>(sections->size());
        for (std::uint32_t sectionIndex = 0U; sectionIndex < sectionCount; ++sectionIndex) {
            const RenderMeshSection& section = sections == nullptr || sections->empty() ? fallbackSection : (*sections)[sectionIndex];
            result.commandLookupScratch.clear();
            result.commandLookupScratch.reserve(instanceCount);
            std::uint32_t culledForSection = 0U;
            for (SceneRenderMeshInstance instance : group.instances) {
                if (!InstanceCanEverBelongToPass(desc.pass, instance, desc.selectedEntityIds)) {
                    continue;
                }
                const std::uint64_t materialAssetId = MaterialAssetForSectionInstance(group, instance, meshResource, section);
                RenderMaterialHandle materialHandle{};
                const RenderMaterialResource* materialResource = validateResources ? nullptr : desc.resolvedMaterialResource;
                if (validateResources) {
                    materialResource = ResolveMaterialOrFallback(instance, materialAssetId, *desc.resources, *desc.resourceMap, materialHandle, result.stats, desc.diagnostics);
                    ValidateMaterialTextureOrFallback(instance, materialAssetId, materialResource, *desc.resources, *desc.resourceMap, result.stats, desc.diagnostics);
                }
                if (!InstanceBelongsToPass(desc.pass, instance, materialResource, desc.selectedEntityIds)) {
                    continue;
                }
                instance.worldBounds = TransformBounds(section.bounds.IsValid() ? section.bounds : (meshResource == nullptr ? RenderBoundsSphere{} : meshResource->bounds), instance.model);
                if (!IsInsideFrustum(frustum, instance.worldBounds)) {
                    ++culledForSection;
                    continue;
                }
                instance.depthBucket = DepthBucket(ViewDepth(desc.camera, instance.worldBounds));
                const MeshCommandLookupKey commandKey{
                    .materialAssetId = materialAssetId,
                    .materialHandleValue = materialHandle.value,
                };
                const auto commandLookupIt = result.commandLookupScratch.find(commandKey);
                MeshDrawCommand* command = commandLookupIt == result.commandLookupScratch.end() ? nullptr : &result.commands[commandLookupIt->second];
                if (command == nullptr && desc.maxDrawCommands != 0U && writeCommandCount >= desc.maxDrawCommands) {
                    ++result.stats.droppedInstanceCount;
                    EmitInstanceDiagnostic(desc.diagnostics, SceneRenderDiagnosticKind::DroppedInstances, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
                    continue;
                }
                if (desc.maxVisibleInstances != 0U && acceptedInstanceCount >= desc.maxVisibleInstances) {
                    ++result.stats.droppedInstanceCount;
                    EmitInstanceDiagnostic(desc.diagnostics, SceneRenderDiagnosticKind::DroppedInstances, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
                    continue;
                }
                if (command == nullptr) {
                    command = &WritableCommand(result, writeCommandCount);
                    command->pass = desc.pass;
                    command->meshAssetId = group.meshAssetId;
                    command->materialAssetId = materialAssetId;
                    command->sectionIndex = sectionIndex;
                    command->materialSlot = section.materialSlot;
                    command->indexStart = section.indexStart;
                    command->indexCount = section.indexCount;
                    command->mesh = meshHandle;
                    command->material = materialHandle;
                    command->meshResource = meshResource;
                    command->materialResource = materialResource;
                    command->state = MeshPassState(desc.pass, meshResource, materialResource);
                    command->instances.reserve(instanceCount);
                    result.stats.meshPipelineScratchInstanceCapacity += static_cast<std::uint32_t>(command->instances.capacity());
                    result.commandLookupScratch.emplace(commandKey, writeCommandCount);
                    result.stats.meshCommandLookupCapacity = std::max<std::uint32_t>(
                        result.stats.meshCommandLookupCapacity,
                        static_cast<std::uint32_t>(result.commandLookupScratch.bucket_count()));
                    ++writeCommandCount;
                }
                command->sortKey += instance.depthBucket;
                command->instances.push_back(instance);
                ++acceptedInstanceCount;
            }

            result.stats.culledInstanceCount += culledForSection;
        }
    }

    result.commands.resize(writeCommandCount);
    for (MeshDrawCommand& command : result.commands) {
        command.depthBucket = command.instances.empty()
            ? 0U
            : static_cast<std::uint16_t>(command.sortKey / static_cast<std::uint64_t>(command.instances.size()));
        command.sortKey = BuildSortKey(desc.pass, command.material, command.materialAssetId, command.mesh, command.meshAssetId, command.depthBucket);
        result.stats.visibleMeshCount += static_cast<std::uint32_t>(command.instances.size());
        ++result.stats.visibleDrawGroupCount;
    }
    std::ranges::sort(result.commands, [](const MeshDrawCommand& lhs, const MeshDrawCommand& rhs) {
        return lhs.sortKey < rhs.sortKey;
    });
    result.stats.meshPipelineCommandCount = static_cast<std::uint32_t>(result.commands.size());
    result.stats.meshPipelineCommandCapacity = static_cast<std::uint32_t>(result.commands.capacity());
    result.stats.meshPipelineSortKeyCount = result.stats.meshPipelineCommandCount;
}

void MeshPipelineProcessor::CountCommandsAsSubmitted(SceneRenderSubmitStats& stats, const std::vector<MeshDrawCommand>& commands) noexcept {
    stats.meshPipelineCommandCount = static_cast<std::uint32_t>(commands.size());
    stats.meshPipelineCommandCapacity = static_cast<std::uint32_t>(commands.capacity());
    stats.meshPipelineSortKeyCount = stats.meshPipelineCommandCount;
    for (const MeshDrawCommand& command : commands) {
        stats.submittedMeshCount += static_cast<std::uint32_t>(command.instances.size());
        ++stats.submittedDrawGroupCount;
        ++stats.submittedDrawCallCount;
    }
}

} // namespace kb::render
