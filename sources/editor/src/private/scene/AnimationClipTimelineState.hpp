#pragma once

#include "engine/scene/AnimationAssets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kb::editor {

enum class AnimationClipTimelineTrackKind : std::uint8_t {
    Bone,
    Transform,
    Morph,
    Curve,
    Event,
    RootMotion,
};

struct AnimationClipTimelineKey {
    float timeSeconds = 0.0F;
    std::uint64_t id = 0U;
};

struct AnimationClipTimelineTrack {
    AnimationClipTimelineTrackKind kind = AnimationClipTimelineTrackKind::Bone;
    kb::scene::SkeletonBoneId boneId = 0U;
    std::string label;
    std::vector<AnimationClipTimelineKey> keys;
};

class AnimationClipTimelineState {
public:
    void SetClip(const kb::scene::AnimationClip& clip) {
        durationSeconds_ = clip.durationSeconds;
        tracks_.clear();
        tracks_.reserve(
            clip.skeletalTracks.size() + clip.tracks.size() + clip.morphTracks.size() +
            clip.curves.size() + 2U);
        for (const kb::scene::AnimationBoneTrack& source : clip.skeletalTracks) {
            tracks_.push_back({ AnimationClipTimelineTrackKind::Bone, source.boneId, "Bone " + std::to_string(source.boneId), BoneKeys(source) });
        }
        for (const kb::scene::AnimationTransformTrack& source : clip.tracks) {
            tracks_.push_back({ AnimationClipTimelineTrackKind::Transform, 0U,
                source.targetPath.empty() || source.targetPath == "." ? "Transform (Owner)" : "Transform " + source.targetPath,
                TransformKeys(source) });
        }
        for (const kb::scene::AnimationMorphTrack& source : clip.morphTracks) {
            tracks_.push_back({ AnimationClipTimelineTrackKind::Morph, 0U, "Morph " + source.morphTarget, MorphKeys(source) });
        }
        for (const kb::scene::AnimationCurveTrack& source : clip.curves) {
            tracks_.push_back({ AnimationClipTimelineTrackKind::Curve, 0U, "Curve " + source.name, CurveKeys(source) });
        }
        if (!clip.events.empty()) {
            std::vector<AnimationClipTimelineKey> keys;
            keys.reserve(clip.events.size());
            for (const kb::scene::AnimationEventKeyframe& event : clip.events) keys.push_back({ event.timeSeconds, event.id });
            SortKeys(keys);
            tracks_.push_back({ AnimationClipTimelineTrackKind::Event, 0U, "Events", std::move(keys) });
        }
        if (clip.rootMotionMode == kb::scene::AnimationRootMotionMode::ExtractFromBone) {
            std::vector<AnimationClipTimelineKey> keys;
            const auto source = std::find_if(clip.skeletalTracks.begin(), clip.skeletalTracks.end(), [&clip](const kb::scene::AnimationBoneTrack& track) {
                return track.boneId == clip.rootMotionBoneId;
            });
            if (source != clip.skeletalTracks.end()) keys = BoneKeys(*source);
            tracks_.push_back({ AnimationClipTimelineTrackKind::RootMotion, clip.rootMotionBoneId,
                "Root Motion (Bone " + std::to_string(clip.rootMotionBoneId) + ")", std::move(keys) });
        } else {
            tracks_.push_back({ AnimationClipTimelineTrackKind::RootMotion, 0U, "Root Motion (Disabled)", {} });
        }
        std::stable_sort(tracks_.begin(), tracks_.end(), [](const AnimationClipTimelineTrack& lhs, const AnimationClipTimelineTrack& rhs) {
            if (lhs.kind != rhs.kind) return lhs.kind < rhs.kind;
            return lhs.label < rhs.label;
        });
        selectedTrack_ = tracks_.empty() ? kNoTrack : 0U;
    }

    [[nodiscard]] float DurationSeconds() const noexcept { return durationSeconds_; }
    [[nodiscard]] const std::vector<AnimationClipTimelineTrack>& Tracks() const noexcept { return tracks_; }
    [[nodiscard]] float Zoom() const noexcept { return zoom_; }
    [[nodiscard]] float PanSeconds() const noexcept { return panSeconds_; }
    [[nodiscard]] bool SnappingEnabled() const noexcept { return snappingEnabled_; }
    [[nodiscard]] std::size_t SelectedTrack() const noexcept { return selectedTrack_; }
    [[nodiscard]] const AnimationClipTimelineTrack* SelectedTrackData() const noexcept {
        return selectedTrack_ < tracks_.size() ? &tracks_[selectedTrack_] : nullptr;
    }
    [[nodiscard]] bool SelectTrack(std::size_t index) noexcept {
        if (index >= tracks_.size() || selectedTrack_ == index) return false;
        selectedTrack_ = index;
        return true;
    }
    [[nodiscard]] bool SelectBoneTrack(kb::scene::SkeletonBoneId boneId) noexcept {
        const auto found = std::find_if(tracks_.begin(), tracks_.end(), [boneId](const AnimationClipTimelineTrack& track) {
            return track.kind == AnimationClipTimelineTrackKind::Bone && track.boneId == boneId;
        });
        return found != tracks_.end() && SelectTrack(static_cast<std::size_t>(found - tracks_.begin()));
    }

    [[nodiscard]] bool SetZoom(float zoom) noexcept {
        if (!std::isfinite(zoom)) return false;
        const float clamped = std::clamp(zoom, 1.0F, 64.0F);
        if (zoom_ == clamped) return false;
        zoom_ = clamped;
        panSeconds_ = std::clamp(panSeconds_, 0.0F, std::max(0.0F, durationSeconds_ - VisibleDurationSeconds()));
        return true;
    }

    [[nodiscard]] bool Pan(float deltaSeconds) noexcept {
        if (!std::isfinite(deltaSeconds)) return false;
        const float next = std::clamp(panSeconds_ + deltaSeconds, 0.0F, std::max(0.0F, durationSeconds_ - VisibleDurationSeconds()));
        if (next == panSeconds_) return false;
        panSeconds_ = next;
        return true;
    }

    [[nodiscard]] bool SetSnappingEnabled(bool enabled) noexcept {
        if (snappingEnabled_ == enabled) return false;
        snappingEnabled_ = enabled;
        return true;
    }

    [[nodiscard]] float VisibleDurationSeconds() const noexcept { return durationSeconds_ / zoom_; }

    [[nodiscard]] float SnapTime(float timeSeconds, float frameRate) const noexcept {
        if (!snappingEnabled_ || !std::isfinite(timeSeconds) || !std::isfinite(frameRate) || frameRate < 1.0F) return timeSeconds;
        return std::round(timeSeconds * frameRate) / frameRate;
    }

private:
    static void SortKeys(std::vector<AnimationClipTimelineKey>& keys) {
        std::stable_sort(keys.begin(), keys.end(), [](const AnimationClipTimelineKey& lhs, const AnimationClipTimelineKey& rhs) {
            return lhs.timeSeconds == rhs.timeSeconds ? lhs.id < rhs.id : lhs.timeSeconds < rhs.timeSeconds;
        });
    }

    [[nodiscard]] static std::vector<AnimationClipTimelineKey> BoneKeys(const kb::scene::AnimationBoneTrack& source) {
        std::vector<AnimationClipTimelineKey> keys;
        keys.reserve(source.keyframes.size());
        for (const kb::scene::AnimationBoneKeyframe& key : source.keyframes) keys.push_back({ key.timeSeconds, 0U });
        SortKeys(keys);
        return keys;
    }

    [[nodiscard]] static std::vector<AnimationClipTimelineKey> TransformKeys(const kb::scene::AnimationTransformTrack& source) {
        std::vector<AnimationClipTimelineKey> keys;
        keys.reserve(source.keyframes.size());
        for (const kb::scene::AnimationTransformKeyframe& key : source.keyframes) keys.push_back({ key.timeSeconds, 0U });
        SortKeys(keys);
        return keys;
    }

    [[nodiscard]] static std::vector<AnimationClipTimelineKey> MorphKeys(const kb::scene::AnimationMorphTrack& source) {
        std::vector<AnimationClipTimelineKey> keys;
        keys.reserve(source.keyframes.size());
        for (const kb::scene::AnimationMorphKeyframe& key : source.keyframes) keys.push_back({ key.timeSeconds, 0U });
        SortKeys(keys);
        return keys;
    }

    [[nodiscard]] static std::vector<AnimationClipTimelineKey> CurveKeys(const kb::scene::AnimationCurveTrack& source) {
        std::vector<AnimationClipTimelineKey> keys;
        keys.reserve(source.keyframes.size());
        for (const kb::scene::AnimationCurveKeyframe& key : source.keyframes) keys.push_back({ key.timeSeconds, 0U });
        SortKeys(keys);
        return keys;
    }

    float durationSeconds_ = 1.0F;
    static constexpr std::size_t kNoTrack = static_cast<std::size_t>(-1);
    std::size_t selectedTrack_ = kNoTrack;
    float zoom_ = 1.0F;
    float panSeconds_ = 0.0F;
    bool snappingEnabled_ = true;
    std::vector<AnimationClipTimelineTrack> tracks_;
};

} // namespace kb::editor
