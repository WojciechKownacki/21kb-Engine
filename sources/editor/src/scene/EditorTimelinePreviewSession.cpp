#include "scene/EditorTimelinePreviewSession.hpp"

#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTimelines.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "scene/EditorPlayModeSelectionSnapshot.hpp"

#include <exception>
#include <stdexcept>

namespace kb::editor {

bool EditorTimelinePreviewSession::Open(kb::scene::Scene& source, kb::scene::SceneEntity owner,
    const kb::project::ProjectDescriptor& project, kb::assets::AssetId assetId,
    const kb::scene::TimelineAsset& working, std::string& error, float initialTime) {
    error.clear();
    if (!source.Entities().IsAlive(owner)) { error = "Select a live Timeline owner in the hierarchy"; return false; }
    try {
        auto candidate = std::make_unique<kb::scene::Scene>(project, kb::scene::SceneMode::Runtime);
        candidate->Assets().Manager().Mounts() = source.Assets().Manager().Mounts();
        for (const auto& metadata : source.Assets().Manager().Registry().All())
            static_cast<void>(candidate->Assets().Manager().RegisterAsset(metadata));
        if (!candidate->Assets().Manager().PublishRuntimeAsset(assetId, std::make_shared<kb::scene::TimelineAsset>(working)))
            throw std::runtime_error("Timeline working copy could not be published to preview");
        EditorPlayModeSelectionSnapshot hierarchy;
        hierarchy.CaptureAuthoredHierarchy(source);
        const auto captured = kb::scene::SceneDocumentService::Capture(source, "Timeline Preview");
        if (!kb::scene::SceneDocumentService::LoadIntoScene(*candidate, captured))
            throw std::runtime_error("Timeline preview scene could not be copied");
        kb::scene::SceneEntity clonedOwner{};
        for (const auto& remap : hierarchy.RecreatedEntities(*candidate)) if (remap.from == owner) clonedOwner = remap.to;
        if (!candidate->Entities().IsAlive(clonedOwner)) throw std::runtime_error("Timeline owner could not be remapped");
        auto scripts = std::make_unique<kb::script::ScriptRuntimeHost>(*candidate);
        if (!scripts->Succeeded() || !scripts->InstallSceneSystem())
            throw std::runtime_error("Timeline preview script runtime could not start");
        candidate->Runtime().SetPlaying(true);
        static_cast<void>(candidate->Runtime().Update(0.0F));
        const auto startupErrors = candidate->Runtime().DrainSceneSystemErrors();
        if (!startupErrors.empty()) throw std::runtime_error(startupErrors.front());
        const auto instance = candidate->Timelines().Create(assetId.value, clonedOwner);
        if (instance == 0) throw std::runtime_error("Timeline preview bindings could not be resolved");
        if (!candidate->Timelines().Seek(instance, initialTime))
            throw std::runtime_error("Timeline preview could not sample the selected time");
        Clear();
        scene_ = std::move(candidate);
        scripts_ = std::move(scripts);
        owner_ = clonedOwner;
        instance_ = instance;
        scene_->Runtime().SynchronizeTransforms();
        return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}

bool EditorTimelinePreviewSession::Seek(float time, std::string& error) {
    error.clear();
    if (!scene_) { error = "Select a Timeline preview owner first"; return false; }
    try {
        if (!scene_->Timelines().Seek(instance_, time)) { error = "Timeline preview seek failed"; return false; }
        scene_->Runtime().SynchronizeTransforms();
        return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}

bool EditorTimelinePreviewSession::Play(float rate, bool loop, std::string& error) {
    error.clear();
    if (!scene_ || !scene_->Timelines().SetPlayback(instance_, rate, loop) || !scene_->Timelines().Play(instance_)) {
        error = "Timeline preview cannot play at this time or rate";
        return false;
    }
    return true;
}

void EditorTimelinePreviewSession::Pause() noexcept {
    if (scene_) static_cast<void>(scene_->Timelines().Pause(instance_));
}

bool EditorTimelinePreviewSession::Tick(float deltaSeconds, std::string& error) {
    error.clear();
    if (!scene_ || !Playing()) return false;
    try {
        static_cast<void>(scene_->Runtime().Update(deltaSeconds));
        const auto errors = scene_->Runtime().DrainSceneSystemErrors();
        if (!errors.empty()) { error = errors.front(); Pause(); return false; }
        return true;
    } catch (const std::exception& failure) { error = failure.what(); Pause(); return false; }
}

void EditorTimelinePreviewSession::Clear() noexcept {
    if (scene_ && releaseHandler_) releaseHandler_(*scene_);
    if (scene_) static_cast<void>(scene_->Timelines().Release(instance_));
    scripts_.reset();
    scene_.reset();
    owner_ = {};
    instance_ = 0;
}

float EditorTimelinePreviewSession::Time() const noexcept {
    return scene_ ? scene_->Timelines().Time(instance_) : 0.0F;
}
bool EditorTimelinePreviewSession::Playing() const noexcept {
    return scene_ && scene_->Timelines().IsPlaying(instance_);
}

} // namespace kb::editor
