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

[[nodiscard]] RenderStaticMeshVertexSkinned ConvertVertex(
    const kb::scene::SkeletalMeshVertex& source,
    const kb::scene::SkeletalMeshSection& section,
    const std::vector<std::uint64_t>& paletteBoneIds) noexcept {
    RenderStaticMeshVertexSkinned vertex{
        .x = source.position.x, .y = source.position.y, .z = source.position.z,
        .nx = source.normal.x, .ny = source.normal.y, .nz = source.normal.z,
        .tx = source.tangent.x, .ty = source.tangent.y, .tz = source.tangent.z, .tw = source.tangent.w,
        .u = source.uv[0], .v = source.uv[1],
    };
    for (std::size_t influence = 0U; influence < source.jointIndices.size(); ++influence) {
        const auto palette = std::lower_bound(paletteBoneIds.begin(), paletteBoneIds.end(),
            section.boneMap[source.jointIndices[influence]]);
        if (palette == paletteBoneIds.end() || *palette != section.boneMap[source.jointIndices[influence]]) {
            return {};
        }
        vertex.joints[influence] = static_cast<std::uint16_t>(palette - paletteBoneIds.begin());
        vertex.weights[influence] = source.jointWeights[influence];
    }
    return vertex;
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
    };
    return desc;
}

std::optional<SkeletalMeshRenderResourceData> SkeletalMeshRenderResourceBuilder::Build(
    const kb::scene::SkeletalMeshAsset& asset) {
    if (!kb::scene::ValidateSkeletalMeshAsset(asset).valid || asset.lods.size() > static_cast<std::size_t>(UINT8_MAX) + 1U) {
        return std::nullopt;
    }
    SkeletalMeshRenderResourceData result{};
    for (const kb::scene::SkeletalMeshLod& lod : asset.lods) {
        result.paletteBoneIds.insert(result.paletteBoneIds.end(), lod.requiredBones.begin(), lod.requiredBones.end());
    }
    std::sort(result.paletteBoneIds.begin(), result.paletteBoneIds.end());
    result.paletteBoneIds.erase(std::unique(result.paletteBoneIds.begin(), result.paletteBoneIds.end()), result.paletteBoneIds.end());
    if (result.paletteBoneIds.empty() || result.paletteBoneIds.size() > kRenderSkinnedVertexJointLimit) return std::nullopt;

    std::unordered_map<std::uint64_t, std::uint32_t> materialSlots;
    for (std::size_t lodIndex = 0U; lodIndex < asset.lods.size(); ++lodIndex) {
        const kb::scene::SkeletalMeshLod& lod = asset.lods[lodIndex];
        const std::uint32_t firstSection = static_cast<std::uint32_t>(result.sections.size());
        for (const kb::scene::SkeletalMeshSection& section : lod.sections) {
            const auto [slot, inserted] = materialSlots.try_emplace(section.materialAssetId,
                static_cast<std::uint32_t>(result.materialSlots.size()));
            if (inserted) result.materialSlots.push_back({ .defaultMaterialAssetId = section.materialAssetId });
            const std::uint32_t indexStart = static_cast<std::uint32_t>(result.indices.size());
            for (std::uint32_t indexOffset = 0U; indexOffset < section.indexCount; ++indexOffset) {
                const kb::scene::SkeletalMeshVertex& source = lod.vertices[lod.indices[section.firstIndex + indexOffset]];
                const RenderStaticMeshVertexSkinned vertex = ConvertVertex(source, section, result.paletteBoneIds);
                if (vertex.weights[0] <= 0.0F && vertex.weights[1] <= 0.0F && vertex.weights[2] <= 0.0F && vertex.weights[3] <= 0.0F) return std::nullopt;
                result.indices.push_back(static_cast<std::uint32_t>(result.vertices.size()));
                result.vertices.push_back(vertex);
            }
            result.sections.push_back({
                .indexStart = indexStart,
                .indexCount = section.indexCount,
                .materialSlot = slot->second,
                .bounds = BoundsOf(asset.conservativeBounds),
                .lodLevel = static_cast<std::uint8_t>(lodIndex),
            });
        }
        result.lods.push_back({
            .firstSection = firstSection,
            .sectionCount = static_cast<std::uint32_t>(result.sections.size()) - firstSection,
            .minScreenCoverage = lod.minScreenCoverage,
        });
    }
    result.bounds = BoundsOf(asset.conservativeBounds);
    if (!result.bounds.IsValid()) return std::nullopt;
    result.RefreshDesc();
    return result;
}

} // namespace kb::render
