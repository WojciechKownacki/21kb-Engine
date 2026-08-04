#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

using SkeletonBoneId = std::uint64_t;

// The skeleton is the single authored owner of rig hierarchy and bind data.
// Runtime poses and skin palettes are derived from this immutable asset; they
// are deliberately not stored here.
struct SkeletonBone {
    SkeletonBoneId id = 0U;
    std::int32_t parentIndex = -1;
    std::string name;
    LocalTransform referencePose{};
    kb::math::Mat4 inverseBind{};
};

struct SkeletonSocket {
    std::string name;
    SkeletonBoneId boneId = 0U;
    LocalTransform localTransform{};
};

struct SkeletonAsset {
    std::vector<SkeletonBone> bones;
    std::vector<SkeletonSocket> sockets;
};

struct SkeletonAssetValidationResult {
    bool valid = false;
    std::string error;
};

// Compatibility is derived from canonical authored data, never persisted as
// an independently editable value. Zero means that the source asset is not a
// valid skeleton and therefore has no compatibility identity.
[[nodiscard]] std::uint64_t SkeletonCompatibilitySignature(const SkeletonAsset& asset);
[[nodiscard]] SkeletonAssetValidationResult ValidateSkeletonAsset(const SkeletonAsset& asset);

} // namespace kb::scene
