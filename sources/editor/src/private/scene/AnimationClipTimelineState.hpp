#pragma once

#include "engine/scene/AnimationAssets.hpp"

#include <algorithm>
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
            tracks_.push_back({ AnimationClipTimelineTrackKind::Bone, "Bone " + std::to_string(source.boneId), BoneKeys(source) });
        }
        for (const kb::scene::AnimationTransformTrack& source : clip.tracks) {
            tracks_.push_back({ AnimationClipTimelineTrackKind::Transform,
                source.targetPath.empty() || source.targetPath == "." ? "Transform (Owner)" : "Transform " + source.targetPath,
                TransformKeys(source) });
        }
        for (const kb::scene::AnimationMorphTrack& source : clip.morphTracks) {
            tracks_.push_back({ AnimationClipTimelineTrackKind::Morph, "Morph " + source.morphTarget, MorphKeys(source) });
        }
        for (const kb::scene::AnimationCurveTrack& source : clip.curves) {
            tracks_.push_back({ AnimationClipTimelineTrackKind::Curve, "Curve " + source.name, CurveKeys(source) });
        }
        if (!clip.events.empty()) {
            std::vector<AnimationClipTimelineKey> keys;
            keys.reserve(clip.events.size());
            for (const kb::scene::AnimationEventKeyframe& event : clip.events) keys.push_back({ event.timeSeconds, event.id });
            SortKeys(keys);
            tracks_.push_back({ AnimationClipTimelineTrackKind::Event, "Events", std::move(keys) });
        }
        if (clip.rootMotionMode == kb::scene::AnimationRootMotionMode::ExtractFromBone) {
            std::vector<AnimationClipTimelineKey> keys;
            const auto source = std::find_if(clip.skeletalTracks.begin(), clip.skeletalTracks.end(), [&clip](const kb::scene::AnimationBoneTrack& track) {
                return track.boneId == clip.rootMotionBoneId;
            });
            if (source != clip.skeletalTracks.end()) keys = BoneKeys(*source);
            tracks_.push_back({ AnimationClipTimelineTrackKind::RootMotion,
                "Root Motion (Bone " + std::to_string(clip.rootMotionBoneId) + ")", std::move(keys) });
        } else {
            tracks_.push_back({ AnimationClipTimelineTrackKind::RootMotion, "Root Motion (Disabled)", {} });
        }
        std::stable_sort(tracks_.begin(), tracks_.end(), [](const AnimationClipTimelineTrack& lhs, const AnimationClipTimelineTrack& rhs) {
            if (lhs.kind != rhs.kind) return lhs.kind < rhs.kind;
            return lhs.label < rhs.label;
        });
    }

    [[nodiscard]] float DurationSeconds() const noexcept { return durationSeconds_; }
    [[nodiscard]] const std::vector<AnimationClipTimelineTrack>& Tracks() const noexcept { return tracks_; }

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
    std::vector<AnimationClipTimelineTrack> tracks_;
};

} // namespace kb::editor
