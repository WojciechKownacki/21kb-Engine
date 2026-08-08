#include "kb/render/resources/SkeletalMeshRenderResourceBuilder.hpp"

#include "engine/scene/SkeletalMeshAsset.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace kb::render {
namespace {

[[nodiscard]] RenderBoundsSphere BoundsOf(const kb::scene::SkeletalMeshBounds& bounds) noexcept {
    return {
        .center = { bounds.center.x, bounds.center.y, bounds.center.z },
        .radius = std::sqrt(bounds.extents.x * bounds.extents.x + bounds.extents.y * bounds.extents.y +
            bounds.extents.z * bounds.extents.z),
    };
}

[[nodiscard]] bool ConvertVertex(
    const kb::scene::SkeletalMeshVertex& source,
    std::span<const std::uint16_t> sectionPaletteIndices,
    RenderStaticMeshVertexSkinned& vertex) noexcept {
    vertex = RenderStaticMeshVertexSkinned{
        .x = source.position.x, .y = source.position.y, .z = source.position.z,
        .nx = source.normal.x, .ny = source.normal.y, .nz = source.normal.z,
        .tx = source.tangent.x, .ty = source.tangent.y, .tz = source.tangent.z, .tw = source.tangent.w,
        .u = source.uv[0], .v = source.uv[1],
    };
    for (std::size_t influence = 0U; influence < source.jointIndices.size(); ++influence) {
        const std::uint16_t sectionJoint = source.jointIndices[influence];
        if (sectionJoint >= sectionPaletteIndices.size()) return false;
        vertex.joints[influence] = sectionPaletteIndices[sectionJoint];
        vertex.weights[influence] = source.jointWeights[influence];
    }
    return true;
}

[[nodiscard]] bool BuildSectionPaletteIndices(
    const kb::scene::SkeletalMeshSection& section,
    std::span<const std::uint64_t> paletteBoneIds,
    std::vector<std::uint16_t>& sectionPaletteIndices) {
    sectionPaletteIndices.clear();
    sectionPaletteIndices.reserve(section.boneMap.size());
    for (const std::uint64_t boneId : section.boneMap) {
        const auto palette = std::lower_bound(paletteBoneIds.begin(), paletteBoneIds.end(), boneId);
        if (palette == paletteBoneIds.end() || *palette != boneId) return false;
        sectionPaletteIndices.push_back(static_cast<std::uint16_t>(palette - paletteBoneIds.begin()));
    }
    return true;
}

[[nodiscard]] std::optional<kb::scene::SkeletalMeshBounds> BoundsOfVertices(
    const std::vector<std::vector<kb::scene::SkeletalMeshVertex>>& verticesByLod) noexcept {
    bool hasVertex = false;
    kb::math::Vec3 minimum{};
    kb::math::Vec3 maximum{};
    for (const std::vector<kb::scene::SkeletalMeshVertex>& vertices : verticesByLod) {
        for (const kb::scene::SkeletalMeshVertex& vertex : vertices) {
            if (!hasVertex) {
                hasVertex = true;
                minimum = vertex.position;
                maximum = vertex.position;
                continue;
            }
            minimum.x = std::min(minimum.x, vertex.position.x);
            minimum.y = std::min(minimum.y, vertex.position.y);
            minimum.z = std::min(minimum.z, vertex.position.z);
            maximum.x = std::max(maximum.x, vertex.position.x);
            maximum.y = std::max(maximum.y, vertex.position.y);
            maximum.z = std::max(maximum.z, vertex.position.z);
        }
    }
    if (!hasVertex) return std::nullopt;
    return kb::scene::SkeletalMeshBounds{
        .center = { (minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F,
            (minimum.z + maximum.z) * 0.5F },
        .extents = { (maximum.x - minimum.x) * 0.5F, (maximum.y - minimum.y) * 0.5F,
            (maximum.z - minimum.z) * 0.5F },
    };
}

} // namespace

RenderMeshDesc& SkeletalMeshRenderResourceData::RefreshDesc() noexcept {
    desc = RenderMeshDesc{
        .vertexData = vertices.empty() ? nullptr : vertices.data(),
        .vertexCount = static_cast<std::uint32_t>(vertices.size()),
        .indices32 = indices.empty() ? nullptr : indices.data(),
        .indexCount = static_cast<std::uint32_t>(indices.size()),
        .vertexFormat = RenderVertexFormat::SkinnedP3N3T4UV2J4W4,
        .indexFormat = RenderIndexFormat::Uint32,
        .sections = sections.empty() ? nullptr : sections.data(),
        .sectionCount = static_cast<std::uint32_t>(sections.size()),
        .materialSlots = materialSlots.empty() ? nullptr : materialSlots.data(),
        .materialSlotCount = static_cast<std::uint32_t>(materialSlots.size()),
        .bounds = bounds,
        .gpuDriven = RenderGpuDrivenMeshDesc{
            .lods = lods.empty() ? nullptr : lods.data(),
            .lodCount = static_cast<std::uint32_t>(lods.size()),
            .allowGpuCulling = false,
            .allowIndirectDraws = false,
            .allowMeshletCulling = false,
        },
        .skinning = { .jointCount = static_cast<std::uint32_t>(paletteBoneIds.size()) },
        .dynamicVertexBuffer = dynamicVertexBuffer,
    };
    return desc;
}

std::optional<SkeletalMeshRenderResourceData> SkeletalMeshRenderResourceBuilder::Build(
    const kb::scene::SkeletalMeshAsset& asset,
    std::span<const std::string> morphTargetNames,
    std::span<const float> morphWeights) {
    if (!kb::scene::ValidateSkeletalMeshAsset(asset).valid) return std::nullopt;
    return BuildValidated(asset, morphTargetNames, morphWeights);
}

std::optional<SkeletalMeshRenderResourceData> SkeletalMeshRenderResourceBuilder::BuildValidated(
    const kb::scene::SkeletalMeshAsset& asset,
    std::span<const std::string> morphTargetNames,
    std::span<const float> morphWeights) {
    if (asset.lods.size() > static_cast<std::size_t>(UINT8_MAX) + 1U) return std::nullopt;
    if (morphTargetNames.size() != morphWeights.size()) return std::nullopt;

    std::size_t totalIndexCount = 0U;
    std::size_t totalSectionCount = 0U;
    std::size_t totalRequiredBoneCount = 0U;
    constexpr std::size_t kMaximumRenderElementCount = std::numeric_limits<std::uint32_t>::max();
    for (const kb::scene::SkeletalMeshLod& lod : asset.lods) {
        if (lod.indices.size() > kMaximumRenderElementCount - totalIndexCount ||
            lod.sections.size() > kMaximumRenderElementCount - totalSectionCount) {
            return std::nullopt;
        }
        totalIndexCount += lod.indices.size();
        totalSectionCount += lod.sections.size();
        if (lod.requiredBones.size() > std::numeric_limits<std::size_t>::max() - totalRequiredBoneCount) {
            return std::nullopt;
        }
        totalRequiredBoneCount += lod.requiredBones.size();
    }

    bool hasActiveMorph = false;
    for (const kb::scene::SkeletalMeshMorphTarget& morph : asset.morphTargets) {
        const auto weight = std::find(morphTargetNames.begin(), morphTargetNames.end(), morph.name);
        if (weight == morphTargetNames.end()) continue;
        const float value = morphWeights[static_cast<std::size_t>(weight - morphTargetNames.begin())];
        if (!std::isfinite(value)) return std::nullopt;
        hasActiveMorph = hasActiveMorph || value != 0.0F;
    }

    SkeletalMeshRenderResourceData result{};
    result.vertices.reserve(totalIndexCount);
    result.indices.reserve(totalIndexCount);
    result.sections.reserve(totalSectionCount);
    result.lods.reserve(asset.lods.size());
    result.materialSlots.reserve(totalSectionCount);
    result.paletteBoneIds.reserve(totalRequiredBoneCount);

    std::vector<std::vector<kb::scene::SkeletalMeshVertex>> morphedVertices;
    if (hasActiveMorph) {
        morphedVertices.reserve(asset.lods.size());
        for (const kb::scene::SkeletalMeshLod& lod : asset.lods) {
            morphedVertices.push_back(lod.vertices);
        }
        for (const kb::scene::SkeletalMeshMorphTarget& morph : asset.morphTargets) {
            const auto weight = std::find(morphTargetNames.begin(), morphTargetNames.end(), morph.name);
            if (weight == morphTargetNames.end()) continue;
            const float value = morphWeights[static_cast<std::size_t>(weight - morphTargetNames.begin())];
            if (value == 0.0F) continue;
            for (const kb::scene::SkeletalMeshMorphDelta& delta : morph.deltas) {
                kb::scene::SkeletalMeshVertex& vertex = morphedVertices[morph.lodIndex][delta.vertexIndex];
                vertex.position = vertex.position + delta.positionDelta * value;
                vertex.normal = vertex.normal + delta.normalDelta * value;
                vertex.tangent.x += delta.tangentDelta.x * value;
                vertex.tangent.y += delta.tangentDelta.y * value;
                vertex.tangent.z += delta.tangentDelta.z * value;
            }
        }
        const std::optional<kb::scene::SkeletalMeshBounds> morphedBounds = BoundsOfVertices(morphedVertices);
        if (!morphedBounds) return std::nullopt;
        result.bounds = BoundsOf(*morphedBounds);
    } else {
        result.bounds = BoundsOf(asset.boundsMode == kb::scene::SkeletalMeshBoundsMode::Fixed
            ? asset.fixedBounds : asset.conservativeBounds);
    }
    result.dynamicVertexBuffer = !asset.morphTargets.empty();
    for (const kb::scene::SkeletalMeshLod& lod : asset.lods) {
        result.paletteBoneIds.insert(result.paletteBoneIds.end(), lod.requiredBones.begin(), lod.requiredBones.end());
    }
    std::sort(result.paletteBoneIds.begin(), result.paletteBoneIds.end());
    result.paletteBoneIds.erase(std::unique(result.paletteBoneIds.begin(), result.paletteBoneIds.end()), result.paletteBoneIds.end());
    if (result.paletteBoneIds.empty() || result.paletteBoneIds.size() > kRenderSkinnedVertexJointLimit) return std::nullopt;

    std::unordered_map<std::uint64_t, std::uint32_t> materialSlots;
    materialSlots.reserve(totalSectionCount);
    std::vector<std::uint16_t> sectionPaletteIndices;
    for (std::size_t lodIndex = 0U; lodIndex < asset.lods.size(); ++lodIndex) {
        const kb::scene::SkeletalMeshLod& lod = asset.lods[lodIndex];
        const std::span<const kb::scene::SkeletalMeshVertex> sourceVertices = hasActiveMorph
            ? std::span<const kb::scene::SkeletalMeshVertex>{ morphedVertices[lodIndex] }
            : std::span<const kb::scene::SkeletalMeshVertex>{ lod.vertices };
        const std::uint32_t firstSection = static_cast<std::uint32_t>(result.sections.size());
        for (const kb::scene::SkeletalMeshSection& section : lod.sections) {
            if (!BuildSectionPaletteIndices(section, result.paletteBoneIds, sectionPaletteIndices)) {
                return std::nullopt;
            }
            const auto [slot, inserted] = materialSlots.try_emplace(section.materialAssetId,
                static_cast<std::uint32_t>(result.materialSlots.size()));
            if (inserted) result.materialSlots.push_back({ .defaultMaterialAssetId = section.materialAssetId });
            const std::uint32_t indexStart = static_cast<std::uint32_t>(result.indices.size());
            for (std::uint32_t indexOffset = 0U; indexOffset < section.indexCount; ++indexOffset) {
                const kb::scene::SkeletalMeshVertex& source =
                    sourceVertices[lod.indices[section.firstIndex + indexOffset]];
                RenderStaticMeshVertexSkinned vertex{};
                if (!ConvertVertex(source, sectionPaletteIndices, vertex) ||
                    (vertex.weights[0] <= 0.0F && vertex.weights[1] <= 0.0F &&
                        vertex.weights[2] <= 0.0F && vertex.weights[3] <= 0.0F)) {
                    return std::nullopt;
                }
                result.indices.push_back(static_cast<std::uint32_t>(result.vertices.size()));
                result.vertices.push_back(vertex);
            }
            result.sections.push_back({
                .indexStart = indexStart,
                .indexCount = section.indexCount,
                .materialSlot = slot->second,
                .bounds = result.bounds,
                .lodLevel = static_cast<std::uint8_t>(lodIndex),
            });
        }
        result.lods.push_back({
            .firstSection = firstSection,
            .sectionCount = static_cast<std::uint32_t>(result.sections.size()) - firstSection,
            .minScreenCoverage = lod.minScreenCoverage,
        });
    }
    if (!result.bounds.IsValid()) return std::nullopt;
    result.RefreshDesc();
    return result;
}

} // namespace kb::render
