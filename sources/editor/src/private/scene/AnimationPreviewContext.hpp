#pragma once

#include "engine/assets/AssetId.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kb::editor {

enum class AnimationPreviewPoseMode : std::uint8_t {
    Reference,
    Animated,
};

class AnimationPreviewTransport {
public:
    [[nodiscard]] bool IsPlaying() const noexcept { return playing_; }
    [[nodiscard]] bool Loops() const noexcept { return loops_; }
    [[nodiscard]] float Speed() const noexcept { return speed_; }
    [[nodiscard]] float NormalizedTime() const noexcept { return normalizedTime_; }
    [[nodiscard]] float FrameRate() const noexcept { return frameRate_; }
    [[nodiscard]] float DurationSeconds() const noexcept { return durationSeconds_; }
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }

    [[nodiscard]] bool SetPlaying(bool playing) noexcept {
        if (playing_ == playing) return false;
        playing_ = playing;
        ++revision_;
        return true;
    }

    [[nodiscard]] bool SetLooping(bool loops) noexcept {
        if (loops_ == loops) return false;
        loops_ = loops;
        ++revision_;
        return true;
    }

    [[nodiscard]] bool SetSpeed(float speed) noexcept {
        if (!std::isfinite(speed) || speed < 0.0F || speed == speed_) return false;
        speed_ = speed;
        ++revision_;
        return true;
    }

    [[nodiscard]] bool SetFrameRate(float frameRate) noexcept {
        if (!std::isfinite(frameRate) || frameRate < 1.0F || frameRate > 1000.0F || frameRate == frameRate_) return false;
        frameRate_ = frameRate;
        ++revision_;
        return true;
    }

    [[nodiscard]] bool SetDurationSeconds(float durationSeconds) noexcept {
        if (!std::isfinite(durationSeconds) || durationSeconds < 0.001F || durationSeconds > 36000.0F ||
            durationSeconds == durationSeconds_) return false;
        durationSeconds_ = durationSeconds;
        ++revision_;
        return true;
    }

    [[nodiscard]] bool Scrub(float normalizedTime) noexcept {
        if (!std::isfinite(normalizedTime)) return false;
        const float clamped = std::clamp(normalizedTime, 0.0F, 1.0F);
        if (clamped == normalizedTime_) return false;
        normalizedTime_ = clamped;
        ++revision_;
        return true;
    }

    [[nodiscard]] bool Step(std::int32_t frames) noexcept {
        if (frames == 0) return false;
        return Move(
            static_cast<double>(frames) /
                (static_cast<double>(frameRate_) * static_cast<double>(durationSeconds_)),
            false);
    }

    [[nodiscard]] bool Advance(float deltaSeconds) noexcept {
        if (!playing_ || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || speed_ == 0.0F) return false;
        return Move(
            static_cast<double>(deltaSeconds) * static_cast<double>(speed_) /
                static_cast<double>(durationSeconds_),
            true);
    }

    void Reset() noexcept {
        playing_ = false;
        loops_ = true;
        speed_ = 1.0F;
        normalizedTime_ = 0.0F;
        frameRate_ = 60.0F;
        durationSeconds_ = 1.0F;
        ++revision_;
    }

private:
    [[nodiscard]] bool Move(double normalizedDelta, bool stopAtEnd) noexcept {
        const double current = static_cast<double>(normalizedTime_);
        double next = current + normalizedDelta;
        if (loops_) {
            next = std::fmod(next, 1.0);
            if (next < 0.0) next += 1.0;
        } else {
            next = std::clamp(next, 0.0, 1.0);
            if (stopAtEnd && next >= 1.0) playing_ = false;
        }
        const float result = static_cast<float>(next);
        if (result == normalizedTime_) return false;
        normalizedTime_ = result;
        ++revision_;
        return true;
    }

    bool playing_ = false;
    bool loops_ = true;
    float speed_ = 1.0F;
    float normalizedTime_ = 0.0F;
    float frameRate_ = 60.0F;
    float durationSeconds_ = 1.0F;
    std::uint64_t revision_ = 1U;
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
    [[nodiscard]] AnimationPreviewTransport& Transport() noexcept { return transport_; }
    [[nodiscard]] const AnimationPreviewTransport& Transport() const noexcept { return transport_; }
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
        transport_.Reset();
    }

private:
    kb::assets::AssetId skeletonAsset_{};
    kb::assets::AssetId skeletalMeshAsset_{};
    kb::assets::AssetId clipAsset_{};
    kb::assets::AssetId controllerAsset_{};
    AnimationPreviewPoseMode poseMode_ = AnimationPreviewPoseMode::Reference;
    AnimationPreviewTransport transport_{};
    std::uint64_t revision_ = 1U;
};

} // namespace kb::editor
