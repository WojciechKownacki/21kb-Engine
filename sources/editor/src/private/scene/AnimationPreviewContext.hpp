#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>

namespace kb::editor {

enum class AnimationPreviewPoseMode : std::uint8_t {
    Reference,
    Animated,
};

// The editor owns exactly one animation preview state. Skeletal-mesh, clip and
// controller documents bind their production runtime inputs to this context;
// they never create independent preview worlds.
class AnimationPreviewContext {
public:
    [[nodiscard]] kb::assets::AssetId SkeletonAsset() const noexcept { return skeletonAsset_; }
    [[nodiscard]] kb::assets::AssetId SkeletalMeshAsset() const noexcept { return skeletalMeshAsset_; }
    [[nodiscard]] kb::assets::AssetId ClipAsset() const noexcept { return clipAsset_; }
    [[nodiscard]] kb::assets::AssetId ControllerAsset() const noexcept { return controllerAsset_; }
    [[nodiscard]] AnimationPreviewPoseMode PoseMode() const noexcept { return poseMode_; }
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }

    void SetAssets(kb::assets::AssetId skeleton, kb::assets::AssetId mesh,
        kb::assets::AssetId clip, kb::assets::AssetId controller) noexcept {
        if (skeletonAsset_ == skeleton && skeletalMeshAsset_ == mesh &&
            clipAsset_ == clip && controllerAsset_ == controller) return;
        skeletonAsset_ = skeleton;
        skeletalMeshAsset_ = mesh;
        clipAsset_ = clip;
        controllerAsset_ = controller;
        ++revision_;
    }

    void SetPoseMode(AnimationPreviewPoseMode mode) noexcept {
        if (poseMode_ == mode) return;
        poseMode_ = mode;
        ++revision_;
    }

    void Clear() noexcept {
        SetAssets({}, {}, {}, {});
        SetPoseMode(AnimationPreviewPoseMode::Reference);
    }

private:
    kb::assets::AssetId skeletonAsset_{};
    kb::assets::AssetId skeletalMeshAsset_{};
    kb::assets::AssetId clipAsset_{};
    kb::assets::AssetId controllerAsset_{};
    AnimationPreviewPoseMode poseMode_ = AnimationPreviewPoseMode::Reference;
    std::uint64_t revision_ = 1U;
};

} // namespace kb::editor
