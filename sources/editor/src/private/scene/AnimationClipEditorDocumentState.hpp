#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/AnimationAssets.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace kb::editor {

class AnimationClipEditorDocumentState {
public:
    void Open(kb::assets::AssetId assetId, kb::scene::AnimationClip clip) {
        assetId_ = assetId;
        saved_ = std::move(clip);
        history_.assign(1U, saved_);
        cursor_ = 0U;
        groupActive_ = false;
        groupHasEdit_ = false;
    }

    [[nodiscard]] const kb::scene::AnimationClip* WorkingCopy() const noexcept {
        return cursor_ < history_.size() ? &history_[cursor_] : nullptr;
    }
    [[nodiscard]] bool CanUndo() const noexcept { return cursor_ > 0U; }
    [[nodiscard]] bool CanRedo() const noexcept { return cursor_ + 1U < history_.size(); }
    [[nodiscard]] bool Dirty() const noexcept { return WorkingCopy() != nullptr && cursor_ != 0U; }

    void BeginGroup() noexcept { groupActive_ = true; groupHasEdit_ = false; }
    void EndGroup() noexcept { groupActive_ = false; groupHasEdit_ = false; }

    [[nodiscard]] bool Undo() noexcept {
        if (!CanUndo()) return false;
        --cursor_;
        return true;
    }
    [[nodiscard]] bool Redo() noexcept {
        if (!CanRedo()) return false;
        ++cursor_;
        return true;
    }
    [[nodiscard]] bool MarkSaved() {
        const kb::scene::AnimationClip* current = WorkingCopy();
        if (current == nullptr) return false;
        saved_ = *current;
        history_.assign(1U, saved_);
        cursor_ = 0U;
        groupActive_ = false;
        groupHasEdit_ = false;
        return true;
    }

    [[nodiscard]] bool UpsertBoneKey(kb::scene::SkeletonBoneId boneId, float timeSeconds, kb::scene::LocalTransform transform) {
        if (!ValidTime(timeSeconds)) return false;
        return Edit([=](kb::scene::AnimationClip& clip) {
            const auto track = std::find_if(clip.skeletalTracks.begin(), clip.skeletalTracks.end(), [boneId](const kb::scene::AnimationBoneTrack& value) {
                return value.boneId == boneId;
            });
            if (track == clip.skeletalTracks.end()) return false;
            const auto key = std::find_if(track->keyframes.begin(), track->keyframes.end(), [timeSeconds](const kb::scene::AnimationBoneKeyframe& value) {
                return value.timeSeconds == timeSeconds;
            });
            if (key != track->keyframes.end()) key->transform = transform;
            else track->keyframes.push_back({ .timeSeconds = timeSeconds, .transform = transform });
            Sort(track->keyframes, [](const kb::scene::AnimationBoneKeyframe& value) { return value.timeSeconds; });
            return true;
        });
    }

    [[nodiscard]] bool RemoveBoneKey(kb::scene::SkeletonBoneId boneId, float timeSeconds) {
        if (!ValidTime(timeSeconds)) return false;
        return Edit([=](kb::scene::AnimationClip& clip) {
            const auto track = std::find_if(clip.skeletalTracks.begin(), clip.skeletalTracks.end(), [boneId](const kb::scene::AnimationBoneTrack& value) {
                return value.boneId == boneId;
            });
            if (track == clip.skeletalTracks.end() || track->keyframes.size() <= 1U) return false;
            const auto key = std::find_if(track->keyframes.begin(), track->keyframes.end(), [timeSeconds](const kb::scene::AnimationBoneKeyframe& value) {
                return value.timeSeconds == timeSeconds;
            });
            if (key == track->keyframes.end()) return false;
            track->keyframes.erase(key);
            return true;
        });
    }

    [[nodiscard]] bool UpsertEvent(kb::scene::AnimationEventId id, float timeSeconds) {
        if (id == 0U || !ValidEventTime(timeSeconds)) return false;
        return Edit([=](kb::scene::AnimationClip& clip) {
            const auto event = std::find_if(clip.events.begin(), clip.events.end(), [id](const kb::scene::AnimationEventKeyframe& value) {
                return value.id == id;
            });
            if (event != clip.events.end()) event->timeSeconds = timeSeconds;
            else clip.events.push_back({ .timeSeconds = timeSeconds, .id = id });
            Sort(clip.events, [](const kb::scene::AnimationEventKeyframe& value) { return value.timeSeconds; },
                [](const kb::scene::AnimationEventKeyframe& value) { return value.id; });
            return true;
        });
    }

    [[nodiscard]] bool RemoveEvent(kb::scene::AnimationEventId id) {
        if (id == 0U) return false;
        return Edit([=](kb::scene::AnimationClip& clip) {
            const auto event = std::find_if(clip.events.begin(), clip.events.end(), [id](const kb::scene::AnimationEventKeyframe& value) {
                return value.id == id;
            });
            if (event == clip.events.end()) return false;
            clip.events.erase(event);
            return true;
        });
    }

private:
    [[nodiscard]] bool ValidTime(float timeSeconds) const noexcept {
        const kb::scene::AnimationClip* clip = WorkingCopy();
        return clip != nullptr && std::isfinite(timeSeconds) && timeSeconds >= 0.0F && timeSeconds <= clip->durationSeconds;
    }
    [[nodiscard]] bool ValidEventTime(float timeSeconds) const noexcept {
        const kb::scene::AnimationClip* clip = WorkingCopy();
        return ValidTime(timeSeconds) && timeSeconds > 0.0F && (!clip->looping || timeSeconds < clip->durationSeconds);
    }

    template <typename Value, typename Primary, typename Secondary = std::nullptr_t>
    static void Sort(std::vector<Value>& values, Primary primary, Secondary secondary = nullptr) {
        std::stable_sort(values.begin(), values.end(), [primary, secondary](const Value& lhs, const Value& rhs) {
            const auto lhsPrimary = primary(lhs);
            const auto rhsPrimary = primary(rhs);
            if (lhsPrimary != rhsPrimary) return lhsPrimary < rhsPrimary;
            if constexpr (!std::is_same_v<Secondary, std::nullptr_t>) {
                return secondary(lhs) < secondary(rhs);
            } else {
                return false;
            }
        });
    }

    template <typename EditFn>
    [[nodiscard]] bool Edit(EditFn&& edit) {
        const kb::scene::AnimationClip* current = WorkingCopy();
        if (current == nullptr) return false;
        kb::scene::AnimationClip candidate = *current;
        if (!edit(candidate)) return false;
        if (groupActive_ && groupHasEdit_ && cursor_ + 1U == history_.size()) {
            history_[cursor_] = std::move(candidate);
        } else {
            history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1U), history_.end());
            history_.push_back(std::move(candidate));
            cursor_ = history_.size() - 1U;
            groupHasEdit_ = groupActive_;
        }
        return true;
    }

    kb::assets::AssetId assetId_{};
    kb::scene::AnimationClip saved_{};
    std::vector<kb::scene::AnimationClip> history_;
    std::size_t cursor_ = 0U;
    bool groupActive_ = false;
    bool groupHasEdit_ = false;
};

} // namespace kb::editor
