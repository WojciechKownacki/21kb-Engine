#include "engine/scene/SkeletalMeshAsset.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace kb::scene {
namespace {

[[nodiscard]] bool IsFinite(float value) noexcept { return std::isfinite(value); }
[[nodiscard]] bool IsFinite(const kb::math::Vec3& value) noexcept {
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}
[[nodiscard]] bool IsFinite(const kb::math::Vec4& value) noexcept {
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

} // namespace

SkeletalMeshAssetValidationResult ValidateSkeletalMeshAsset(const SkeletalMeshAsset& asset) {
    if (asset.skeletonAssetId == 0U || asset.skeletonCompatibilitySignature == 0U || asset.lods.empty() ||
        !IsFinite(asset.conservativeBounds.center) || !IsFinite(asset.conservativeBounds.extents) ||
        asset.conservativeBounds.extents.x < 0.0F || asset.conservativeBounds.extents.y < 0.0F || asset.conservativeBounds.extents.z < 0.0F) {
        return { false, "Skeletal mesh has an invalid skeleton reference, LOD set, or conservative bounds." };
    }
    std::unordered_set<std::string> morphNames;
    for (std::size_t lodIndex = 0U; lodIndex < asset.lods.size(); ++lodIndex) {
        const SkeletalMeshLod& lod = asset.lods[lodIndex];
        if (lod.vertices.empty() || lod.indices.empty() || lod.indices.size() % 3U != 0U || lod.sections.empty() ||
            !IsFinite(lod.minScreenCoverage) || lod.minScreenCoverage < 0.0F || lod.minScreenCoverage > 1.0F) {
            return { false, "Skeletal mesh LOD " + std::to_string(lodIndex) + " is incomplete." };
        }
        std::unordered_set<SkeletonBoneId> required;
        for (const SkeletonBoneId bone : lod.requiredBones) {
            if (bone == 0U || !required.insert(bone).second) return { false, "Skeletal mesh LOD has duplicate required bones." };
        }
        for (const SkeletalMeshVertex& vertex : lod.vertices) {
            float weightSum = 0.0F;
            for (std::size_t influence = 0U; influence < vertex.jointWeights.size(); ++influence) {
                if (!IsFinite(vertex.jointWeights[influence]) || vertex.jointWeights[influence] < 0.0F) return { false, "Skeletal mesh has invalid joint weights." };
                weightSum += vertex.jointWeights[influence];
            }
            if (!IsFinite(vertex.position) || !IsFinite(vertex.normal) || !IsFinite(vertex.tangent) ||
                !IsFinite(vertex.uv[0]) || !IsFinite(vertex.uv[1]) || std::fabs(weightSum - 1.0F) > 0.0001F) return { false, "Skeletal mesh has non-finite vertex data or non-normalized weights." };
        }
        for (const std::uint32_t index : lod.indices) if (index >= lod.vertices.size()) return { false, "Skeletal mesh index is outside its LOD vertex range." };
        std::uint32_t previousSectionEnd = 0U;
        for (const SkeletalMeshSection& section : lod.sections) {
            if (section.indexCount == 0U || section.indexCount % 3U != 0U || section.firstIndex != previousSectionEnd ||
                section.firstIndex > lod.indices.size() || section.indexCount > lod.indices.size() - section.firstIndex || section.boneMap.empty()) return { false, "Skeletal mesh section range or bone map is invalid." };
            std::unordered_set<SkeletonBoneId> sectionBones;
            for (const SkeletonBoneId bone : section.boneMap) if (bone == 0U || !sectionBones.insert(bone).second) return { false, "Skeletal mesh section has duplicate or invalid bone ids." };
            for (std::uint32_t indexOffset = 0U; indexOffset < section.indexCount; ++indexOffset) {
                const SkeletalMeshVertex& vertex = lod.vertices[lod.indices[section.firstIndex + indexOffset]];
                for (const std::uint16_t joint : vertex.jointIndices) if (joint >= section.boneMap.size()) return { false, "Skeletal mesh joint index is outside its section bone map." };
            }
            previousSectionEnd += section.indexCount;
        }
        if (previousSectionEnd != lod.indices.size()) return { false, "Skeletal mesh sections do not cover every LOD index." };
    }
    for (const SkeletalMeshMorphTarget& morph : asset.morphTargets) {
        if (morph.name.empty() || !morphNames.insert(morph.name).second || morph.lodIndex >= asset.lods.size() || morph.deltas.empty()) return { false, "Skeletal mesh morph target is invalid." };
        std::uint32_t previousVertex = 0U;
        bool first = true;
        for (const SkeletalMeshMorphDelta& delta : morph.deltas) {
            if (delta.vertexIndex >= asset.lods[morph.lodIndex].vertices.size() || (!first && delta.vertexIndex <= previousVertex) ||
                !IsFinite(delta.positionDelta) || !IsFinite(delta.normalDelta) || !IsFinite(delta.tangentDelta)) return { false, "Skeletal mesh morph delta is invalid." };
            first = false; previousVertex = delta.vertexIndex;
        }
    }
    return { true, {} };
}

} // namespace kb::scene
