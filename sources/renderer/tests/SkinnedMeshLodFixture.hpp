#pragma once

#include "engine/scene/SkeletalMeshAsset.hpp"
#include "engine/scene/SkeletonAsset.hpp"

#include <cmath>
#include <cstdint>
#include <numbers>

namespace kb::render::tests {

// Bones 1 and 2 of the fixture skeleton: bone 2 sits off its parent in the reference pose, so the sphere is bent.
[[nodiscard]] inline kb::scene::SkeletonAsset MakeSkinnedSphereSkeleton() {
    kb::scene::SkeletonAsset skeleton;
    skeleton.bones.push_back({ .id = 1U, .name = "Root" });
    kb::scene::SkeletonBone child{ .id = 2U, .parentIndex = 0, .name = "Top" };
    child.referencePose.position = { 0.35F, 0.0F, 0.0F };
    skeleton.bones.push_back(std::move(child));
    return skeleton;
}

// A unit sphere of rings * slices * 2 - slices * 2 triangles in one section, its skin weight passing from bone 1 at
// the bottom to bone 2 at the top.
[[nodiscard]] inline kb::scene::SkeletalMeshAsset MakeSkinnedSphereAsset(
    unsigned rings, unsigned slices, std::uint64_t skeletonAssetId, std::uint64_t compatibilitySignature) {
    kb::scene::SkeletalMeshAsset mesh;
    mesh.skeletonAssetId = skeletonAssetId;
    mesh.skeletonCompatibilitySignature = compatibilitySignature;
    mesh.conservativeBounds = { .center = { 0.35F, 0.0F, 0.0F }, .extents = { 1.35F, 1.0F, 1.0F } };
    mesh.fixedBounds = mesh.conservativeBounds;
    kb::scene::SkeletalMeshLod lod;
    for (unsigned ring = 0U; ring <= rings; ++ring) {
        for (unsigned slice = 0U; slice <= slices; ++slice) {
            const double theta = std::numbers::pi * ring / rings;
            const double phi = 2.0 * std::numbers::pi * slice / slices;
            const kb::math::Vec3 point{ static_cast<float>(std::sin(theta) * std::cos(phi)), static_cast<float>(std::cos(theta)),
                static_cast<float>(std::sin(theta) * std::sin(phi)) };
            const float top = (point.y + 1.0F) * 0.5F;
            lod.vertices.push_back({ .position = point, .normal = point,
                .uv = { static_cast<float>(double(slice) / slices), static_cast<float>(double(ring) / rings) },
                .jointIndices = { 0U, 1U, 0U, 0U }, .jointWeights = { 1.0F - top, top, 0.0F, 0.0F } });
        }
    }
    for (unsigned ring = 0U; ring < rings; ++ring) {
        for (unsigned slice = 0U; slice < slices; ++slice) {
            const unsigned a = ring * (slices + 1U) + slice, b = a + slices + 1U, c = a + 1U, d = b + 1U;
            if (ring > 0U) lod.indices.insert(lod.indices.end(), { a, c, b });
            if (ring + 1U < rings) lod.indices.insert(lod.indices.end(), { c, d, b });
        }
    }
    lod.sections.push_back({ .indexCount = static_cast<std::uint32_t>(lod.indices.size()), .materialAssetId = 0U, .boneMap = { 1U, 2U } });
    lod.requiredBones = { 1U, 2U };
    kb::scene::BuildSkeletalMeshLodBoneBounds(lod);
    mesh.lods.push_back(std::move(lod));
    return mesh;
}

} // namespace kb::render::tests
