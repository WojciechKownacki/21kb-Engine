#include "scene/EditorSceneContext.hpp"

#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <algorithm>

namespace kb::editor {

bool EditorSceneContext::OpenTimelineEditorAsset(kb::assets::AssetId id) {
    if (timelineEditorDocument_.Dirty() && timelineEditorDocument_.AssetId() != id) {
        console_.Error("Timeline Editor", "Save the current Timeline before opening another asset.");
        return false;
    }
    if (timelineEditorDocument_.AssetId() == id) return true;
    auto asset = scene_->Assets().Manager().Load<kb::scene::TimelineAsset>(id);
    if (!asset.IsLoaded() || !timelineEditorDocument_.Open(id, *asset)) return false;
    timelineEditorPreview_.Clear();
    timelineEditorOwner_ = {};
    timelineEditorFirstRow_ = timelineEditorSelectedRow_ = 0;
    const auto selected = SelectedEntity();
    if (scene_->Entities().IsAlive(selected)) static_cast<void>(SetTimelineEditorPreviewOwner(selected));
    return true;
}

bool EditorSceneContext::EditTimelineAsset(const std::function<bool(kb::scene::TimelineAsset&)>& edit) {
    auto candidate = timelineEditorDocument_;
    if (!candidate.Apply(edit)) {
        console_.Error("Timeline Editor", "The edit is not a valid Timeline document.");
        return false;
    }
    const auto* working = candidate.WorkingCopy();
    if (!scene_->Entities().IsAlive(timelineEditorOwner_)) ClearTimelineEditorPreview();
    if (scene_->Entities().IsAlive(timelineEditorOwner_)) {
        std::string error;
        const float time = std::clamp(timelineEditorPreview_.Time(), 0.0F, working->durationSeconds);
        if (!timelineEditorPreview_.Open(*scene_, timelineEditorOwner_, project_, candidate.AssetId(), *working, error, time)) {
            console_.Error("Timeline Editor", error);
            return false;
        }
    }
    timelineEditorDocument_ = std::move(candidate);
    return true;
}

bool EditorSceneContext::UndoTimelineEditorEdit() {
    auto previous = timelineEditorDocument_;
    if (!timelineEditorDocument_.Undo()) return false;
    if (scene_->Entities().IsAlive(timelineEditorOwner_) && !SetTimelineEditorPreviewOwner(timelineEditorOwner_)) {
        timelineEditorDocument_ = std::move(previous);
        return false;
    }
    return true;
}

bool EditorSceneContext::RedoTimelineEditorEdit() {
    auto previous = timelineEditorDocument_;
    if (!timelineEditorDocument_.Redo()) return false;
    if (scene_->Entities().IsAlive(timelineEditorOwner_) && !SetTimelineEditorPreviewOwner(timelineEditorOwner_)) {
        timelineEditorDocument_ = std::move(previous);
        return false;
    }
    return true;
}

bool EditorSceneContext::SaveTimelineEditorAsset() {
    const auto id = timelineEditorDocument_.AssetId();
    const auto* working = timelineEditorDocument_.WorkingCopy();
    const auto* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (!working || !metadata || !kb::scene::TimelineAssetIO::Save(metadata->physicalPath, *working)) {
        console_.Error("Timeline Editor", "Timeline validation or atomic save failed.");
        return false;
    }
    if (!scene_->Assets().Manager().RefreshAsset(id)) {
        console_.Error("Timeline Editor", "Timeline was saved, but asset refresh failed.");
        return false;
    }
    timelineEditorDocument_.MarkSaved();
    return true;
}

bool EditorSceneContext::RevertTimelineEditorAsset() {
    const auto id = timelineEditorDocument_.AssetId();
    if (!id.IsValid() || !scene_->Assets().Manager().RefreshAsset(id)) return false;
    auto asset = scene_->Assets().Manager().Load<kb::scene::TimelineAsset>(id);
    if (!asset.IsLoaded()) return false;
    auto candidate = timelineEditorDocument_;
    if (!candidate.Open(id, *asset)) return false;
    if (scene_->Entities().IsAlive(timelineEditorOwner_)) {
        std::string error;
        if (!timelineEditorPreview_.Open(*scene_, timelineEditorOwner_, project_, id, *asset, error,
                std::clamp(timelineEditorPreview_.Time(), 0.0F, asset->durationSeconds))) {
            console_.Error("Timeline Editor", error); return false;
        }
    }
    timelineEditorDocument_ = std::move(candidate);
    return true;
}

bool EditorSceneContext::SetTimelineEditorPreviewOwner(kb::scene::SceneEntity owner) {
    const auto* working = timelineEditorDocument_.WorkingCopy();
    if (!working) return false;
    std::string error;
    const float time = std::clamp(timelineEditorPreview_.Time(), 0.0F, working->durationSeconds);
    if (!timelineEditorPreview_.Open(*scene_, owner, project_, timelineEditorDocument_.AssetId(), *working, error, time)) {
        console_.Error("Timeline Editor", error);
        return false;
    }
    timelineEditorOwner_ = owner;
    return true;
}

bool EditorSceneContext::SeekTimelineEditorPreview(float time) {
    timelineEditorPreview_.Pause();
    std::string error;
    const bool result = timelineEditorPreview_.Seek(time, error);
    if (!result && !error.empty()) console_.Error("Timeline Editor", error);
    return result;
}

bool EditorSceneContext::PlayTimelineEditorPreview(float rate, bool loop) {
    std::string error;
    const bool result = timelineEditorPreview_.Play(rate, loop, error);
    if (!result) console_.Error("Timeline Editor", error);
    return result;
}

void EditorSceneContext::PauseTimelineEditorPreview() noexcept { timelineEditorPreview_.Pause(); }

bool EditorSceneContext::TickTimelineEditorPreview(float deltaSeconds) {
    std::string error;
    const bool result = timelineEditorPreview_.Tick(deltaSeconds, error);
    if (!error.empty()) console_.Error("Timeline Editor", error);
    return result;
}

} // namespace kb::editor
