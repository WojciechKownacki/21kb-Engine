#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/SkeletonAsset.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

struct SkeletalMeshVertex {
    kb::math::Vec3 position{};
    kb::math::Vec3 normal{ 0.0F, 1.0F, 0.0F };
    kb::math::Vec4 tangent{ 1.0F, 0.0F, 0.0F, 1.0F };
    std::array<float, 2U> uv{};
    std::array<std::uint16_t, 4U> jointIndices{};
    std::array<float, 4U> jointWeights{ 1.0F, 0.0F, 0.0F, 0.0F };
};

struct SkeletalMeshSection {
    std::uint32_t firstIndex = 0U;
    std::uint32_t indexCount = 0U;
    std::uint64_t materialAssetId = 0U;
    std::vector<SkeletonBoneId> boneMap;
};

struct SkeletalMeshLod {
    std::vector<SkeletalMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SkeletalMeshSection> sections;
    std::vector<SkeletonBoneId> requiredBones;
    float minScreenCoverage = 0.0F;
};

struct SkeletalMeshMorphDelta {
    std::uint32_t vertexIndex = 0U;
    kb::math::Vec3 positionDelta{};
    kb::math::Vec3 normalDelta{};
    kb::math::Vec3 tangentDelta{};
};

struct SkeletalMeshMorphTarget {
    std::string name;
    std::uint32_t lodIndex = 0U;
    std::vector<SkeletalMeshMorphDelta> deltas;
};

// Bounds are authored in mesh-local space and must contain every imported
// vertex position. Later animated-bounds work derives a tighter runtime bound
// without replacing this conservative authoring contract.
struct SkeletalMeshBounds {
    kb::math::Vec3 center{};
    kb::math::Vec3 extents{};
};

struct SkeletalMeshAsset {
    std::uint64_t skeletonAssetId = 0U;
    std::uint64_t skeletonCompatibilitySignature = 0U;
    std::vector<SkeletalMeshLod> lods;
    SkeletalMeshBounds conservativeBounds{};
    std::vector<SkeletalMeshMorphTarget> morphTargets;
};

struct SkeletalMeshAssetValidationResult {
    bool valid = false;
    std::string error;
};

[[nodiscard]] SkeletalMeshAssetValidationResult ValidateSkeletalMeshAsset(const SkeletalMeshAsset& asset);

} // namespace kb::scene
