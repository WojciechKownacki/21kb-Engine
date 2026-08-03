#include "engine/scene/SkeletalMeshAsset.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
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

[[nodiscard]] bool IsValidBounds(const SkeletalMeshBounds& bounds) noexcept {
    return IsFinite(bounds.center) && IsFinite(bounds.extents) &&
        bounds.extents.x >= 0.0F && bounds.extents.y >= 0.0F && bounds.extents.z >= 0.0F;
}

[[nodiscard]] bool Contains(const SkeletalMeshBounds& bounds, const kb::math::Vec3& point) noexcept {
    constexpr float kEpsilon = 0.0001F;
    return point.x >= bounds.center.x - bounds.extents.x - kEpsilon &&
        point.x <= bounds.center.x + bounds.extents.x + kEpsilon &&
        point.y >= bounds.center.y - bounds.extents.y - kEpsilon &&
        point.y <= bounds.center.y + bounds.extents.y + kEpsilon &&
        point.z >= bounds.center.z - bounds.extents.z - kEpsilon &&
        point.z <= bounds.center.z + bounds.extents.z + kEpsilon;
}

struct BoundsAccumulator {
    kb::math::Vec3 minimum{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    kb::math::Vec3 maximum{
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    bool hasValue = false;

    void Add(kb::math::Vec3 value) noexcept {
        minimum.x = std::min(minimum.x, value.x); minimum.y = std::min(minimum.y, value.y); minimum.z = std::min(minimum.z, value.z);
        maximum.x = std::max(maximum.x, value.x); maximum.y = std::max(maximum.y, value.y); maximum.z = std::max(maximum.z, value.z);
        hasValue = true;
    }

    [[nodiscard]] SkeletalMeshBounds Finish() const noexcept {
        return {
            .center = { (minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F, (minimum.z + maximum.z) * 0.5F },
            .extents = { (maximum.x - minimum.x) * 0.5F, (maximum.y - minimum.y) * 0.5F, (maximum.z - minimum.z) * 0.5F },
        };
    }
};

[[nodiscard]] kb::math::Vec3 TransformPoint(const kb::math::Mat4& matrix, kb::math::Vec3 point) noexcept {
    const kb::math::Vec4 transformed = matrix * kb::math::Vec4{ point.x, point.y, point.z, 1.0F };
    if (!std::isfinite(transformed.w) || std::abs(transformed.w) <= 0.000001F) return {};
    const float inverseW = 1.0F / transformed.w;
    return { transformed.x * inverseW, transformed.y * inverseW, transformed.z * inverseW };
}

} // namespace

void BuildSkeletalMeshLodBoneBounds(SkeletalMeshLod& lod) {
    std::map<SkeletonBoneId, BoundsAccumulator> accumulators;
    for (const SkeletalMeshSection& section : lod.sections) {
        if (section.firstIndex > lod.indices.size() || section.indexCount > lod.indices.size() - section.firstIndex) continue;
        for (std::uint32_t offset = 0U; offset < section.indexCount; ++offset) {
            const std::uint32_t vertexIndex = lod.indices[section.firstIndex + offset];
            if (vertexIndex >= lod.vertices.size()) continue;
            const SkeletalMeshVertex& vertex = lod.vertices[vertexIndex];
            for (std::size_t influence = 0U; influence < vertex.jointWeights.size(); ++influence) {
                if (vertex.jointWeights[influence] <= 0.0F) continue;
                const std::uint16_t jointIndex = vertex.jointIndices[influence];
                if (jointIndex >= section.boneMap.size()) continue;
                accumulators[section.boneMap[jointIndex]].Add(vertex.position);
            }
        }
    }
    lod.boneBounds.clear();
    lod.boneBounds.reserve(accumulators.size());
    for (const auto& [boneId, bounds] : accumulators) {
        if (boneId != 0U && bounds.hasValue) {
            const SkeletalMeshBounds finished = bounds.Finish();
            lod.boneBounds.push_back({ .boneId = boneId, .center = finished.center, .extents = finished.extents });
        }
    }
}

std::optional<SkeletalMeshBounds> EvaluateSkeletalMeshAnimatedBounds(
    const SkeletalMeshAsset& asset,
    std::uint32_t lodIndex,
    std::span<const SkeletonBoneId> boneIds,
    std::span<const kb::math::Mat4> skinMatrices) noexcept {
    if (asset.boundsMode == SkeletalMeshBoundsMode::Fixed) return asset.fixedBounds;
    if (lodIndex >= asset.lods.size() || boneIds.size() != skinMatrices.size()) return std::nullopt;
    const SkeletalMeshLod& lod = asset.lods[lodIndex];
    if (lod.boneBounds.empty()) return asset.conservativeBounds;
    BoundsAccumulator accumulator;
    for (const SkeletalMeshBoneBounds& bounds : lod.boneBounds) {
        const auto matrix = std::find(boneIds.begin(), boneIds.end(), bounds.boneId);
        if (matrix == boneIds.end()) return std::nullopt;
        const kb::math::Mat4& skin = skinMatrices[static_cast<std::size_t>(matrix - boneIds.begin())];
        for (const kb::math::Vec4& column : skin.columns) {
            if (!IsFinite(column)) return std::nullopt;
        }
        for (std::uint32_t corner = 0U; corner < 8U; ++corner) {
            const kb::math::Vec3 point{
                bounds.center.x + ((corner & 1U) == 0U ? -bounds.extents.x : bounds.extents.x),
                bounds.center.y + ((corner & 2U) == 0U ? -bounds.extents.y : bounds.extents.y),
                bounds.center.z + ((corner & 4U) == 0U ? -bounds.extents.z : bounds.extents.z),
            };
            accumulator.Add(TransformPoint(skin, point));
        }
    }
    return accumulator.hasValue ? std::optional<SkeletalMeshBounds>{ accumulator.Finish() } : std::nullopt;
}

SkeletalMeshAssetValidationResult ValidateSkeletalMeshAsset(const SkeletalMeshAsset& asset) {
    if (asset.skeletonAssetId == 0U || asset.skeletonCompatibilitySignature == 0U || asset.lods.empty() ||
        !IsValidBounds(asset.conservativeBounds) ||
        !IsValidBounds(asset.fixedBounds) ||
        asset.boundsMode != SkeletalMeshBoundsMode::ImportedConservative &&
            asset.boundsMode != SkeletalMeshBoundsMode::Fixed) {
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
        SkeletonBoneId previousBoundBone = 0U;
        for (const SkeletalMeshBoneBounds& bounds : lod.boneBounds) {
            const SkeletalMeshBounds value{ .center = bounds.center, .extents = bounds.extents };
            if (bounds.boneId == 0U || !required.contains(bounds.boneId) ||
                !IsValidBounds(value) || bounds.boneId <= previousBoundBone) {
                return { false, "Skeletal mesh LOD has invalid per-bone bounds." };
            }
            previousBoundBone = bounds.boneId;
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
    for (const SkeletalMeshLod& lod : asset.lods) {
        for (const SkeletalMeshVertex& vertex : lod.vertices) {
            if (!Contains(asset.conservativeBounds, vertex.position)) {
                return { false, "Skeletal mesh conservative bounds do not contain every imported vertex." };
            }
            if (asset.boundsMode == SkeletalMeshBoundsMode::Fixed &&
                !Contains(asset.fixedBounds, vertex.position)) {
                return { false, "Skeletal mesh fixed bounds do not contain every imported vertex." };
            }
        }
    }
    return { true, {} };
}

} // namespace kb::scene
