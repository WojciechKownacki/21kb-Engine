#pragma once

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kb::scene::skeletal_import {

// Where an import's skeleton goes. A skeleton already in the project is reused when it is
// the same one; a source of animations only also lands on a skeleton of the same rig - the
// same bones, hierarchy and rest pose within a millimetre - so its clips play on the character
// imported before it without retargeting.
struct SkeletonChoice {
    kb::assets::AssetId id{};
    std::filesystem::path virtualPath;
    bool reuse = false;
    bool update = false;
    // Set when the clips must be rewritten onto this skeleton's bone ids.
    std::optional<SkeletonAsset> sameRig;
};

// Every bone of `existing` is in `imported` under its name, below the same parent, at the same
// rest pose. `imported` may hold more: an exporter without skins keeps the armature object as
// a node above the bones, which no clip needs.
[[nodiscard]] inline bool SameRig(const SkeletonAsset& existing, const SkeletonAsset& imported) {
    constexpr float kTolerance = 0.001F;
    const auto close = [](float a, float b) { return std::abs(a - b) <= kTolerance; };
    std::unordered_map<std::string, std::size_t> existingByName;
    std::unordered_map<std::string, std::size_t> importedByName;
    for (std::size_t index = 0U; index < existing.bones.size(); ++index) existingByName.emplace(existing.bones[index].name, index);
    for (std::size_t index = 0U; index < imported.bones.size(); ++index) importedByName.emplace(imported.bones[index].name, index);
    if (existing.bones.empty()) return false;
    const auto parentName = [](const SkeletonAsset& skeleton, const SkeletonBone& bone, const auto& names) -> std::string {
        if (bone.parentIndex < 0) return {};
        const std::string& name = skeleton.bones[static_cast<std::size_t>(bone.parentIndex)].name;
        return names.contains(name) ? name : std::string{};
    };
    for (const SkeletonBone& a : existing.bones) {
        const auto found = importedByName.find(a.name);
        if (found == importedByName.end()) return false;
        const SkeletonBone& b = imported.bones[found->second];
        if (parentName(existing, a, existingByName) != parentName(imported, b, existingByName)) return false;
        const LocalTransform& p = a.referencePose;
        const LocalTransform& q = b.referencePose;
        const float dot = p.rotation.x * q.rotation.x + p.rotation.y * q.rotation.y +
            p.rotation.z * q.rotation.z + p.rotation.w * q.rotation.w;
        if (!close(p.position.x, q.position.x) || !close(p.position.y, q.position.y) ||
            !close(p.position.z, q.position.z) || std::abs(dot) < 1.0F - kTolerance ||
            !close(p.scale.x, q.scale.x) || !close(p.scale.y, q.scale.y) || !close(p.scale.z, q.scale.z)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline std::optional<SkeletonChoice> ChooseSkeleton(
    const kb::assets::AssetManager& manager,
    const SkeletonAsset& imported,
    bool animationsOnly,
    const std::string& normalizedFolder,
    const std::string& stem,
    std::string* error) {
    const std::uint64_t signature = SkeletonCompatibilitySignature(imported);
    if (signature == 0U) {
        if (error != nullptr) *error = "The import produced a skeleton without a compatibility signature.";
        return std::nullopt;
    }
    std::vector<kb::assets::AssetMetadata> skeletons = manager.Registry().ByType(kSkeletonAssetType);
    std::sort(skeletons.begin(), skeletons.end(), [](const auto& left, const auto& right) {
        return left.id.value < right.id.value;
    });
    SkeletonChoice choice{};
    for (const kb::assets::AssetMetadata& metadata : skeletons) {
        std::string loadError;
        const auto existing = SkeletonAssetIO::Load(metadata.physicalPath, &loadError);
        if (existing && SkeletonCompatibilitySignature(*existing) == signature) {
            choice.id = metadata.id;
            choice.virtualPath = metadata.virtualPath;
            choice.reuse = true;
            return choice;
        }
    }
    if (animationsOnly) {
        for (const kb::assets::AssetMetadata& metadata : skeletons) {
            std::string loadError;
            auto existing = SkeletonAssetIO::Load(metadata.physicalPath, &loadError);
            if (existing && SameRig(*existing, imported)) {
                choice.id = metadata.id;
                choice.virtualPath = metadata.virtualPath;
                choice.reuse = true;
                choice.sameRig = std::move(*existing);
                return choice;
            }
        }
    }
    const std::filesystem::path candidatePath = std::filesystem::path{ normalizedFolder } / (stem + kSkeletonAssetExtension);
    if (const kb::assets::AssetMetadata* occupied = manager.Registry().FindByPath(candidatePath); occupied != nullptr) {
        if (occupied->type != kSkeletonAssetType) {
            if (error != nullptr) *error = "The import's skeleton path is occupied by an asset that is not a Skeleton: " +
                candidatePath.generic_string();
            return std::nullopt;
        }
        choice.id = occupied->id;
        choice.virtualPath = occupied->virtualPath;
        choice.update = true;
        return choice;
    }
    choice.id = kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath(candidatePath) + ":" + kSkeletonAssetType);
    choice.virtualPath = candidatePath;
    return choice;
}

// Rewrites clips made against `from` onto the bones of `to` (the same rig), by bone name; tracks
// of bones `to` does not have (the armature object) are dropped.
inline void MoveClipsToRig(std::vector<AnimationClip>& clips, const SkeletonAsset& from, const SkeletonAsset& to,
    std::uint64_t toAssetId) {
    std::unordered_map<std::string, SkeletonBoneId> byName;
    for (const SkeletonBone& bone : to.bones) byName.emplace(bone.name, bone.id);
    std::unordered_map<SkeletonBoneId, SkeletonBoneId> remap;
    for (const SkeletonBone& bone : from.bones) {
        if (const auto found = byName.find(bone.name); found != byName.end()) remap.emplace(bone.id, found->second);
    }
    const std::uint64_t signature = SkeletonCompatibilitySignature(to);
    for (AnimationClip& clip : clips) {
        clip.targetSkeletonAssetId = toAssetId;
        clip.targetSkeletonCompatibilitySignature = signature;
        std::erase_if(clip.skeletalTracks, [&](const AnimationBoneTrack& track) { return !remap.contains(track.boneId); });
        for (AnimationBoneTrack& track : clip.skeletalTracks) track.boneId = remap.at(track.boneId);
        if (const auto root = remap.find(clip.rootMotionBoneId); root != remap.end()) {
            clip.rootMotionBoneId = root->second;
        } else {
            clip.rootMotionBoneId = 0U;
            clip.rootMotionMode = AnimationRootMotionMode::None;
        }
    }
}

} // namespace kb::scene::skeletal_import
