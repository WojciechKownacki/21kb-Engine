#include "scene/audio/EditorSceneAudioSettingsService.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"

#include <utility>

namespace kb::editor {

EditorSceneAudioSettingsService::EditorSceneAudioSettingsService(
    kb::scene::Scene& scene,
    Executor executor)
    : scene_(scene)
    , executor_(std::move(executor)) {}

bool EditorSceneAudioSettingsService::IsMixerCandidateValid(kb::assets::AssetId id) {
    if (!id.IsValid()) {
        return true;
    }
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || metadata->type != kb::audio::kAudioMixerAssetType) {
        return false;
    }
    const kb::assets::AssetHandle<kb::audio::AudioMixerAsset> asset =
        manager.Load<kb::audio::AudioMixerAsset>(id);
    return asset.IsLoaded() && kb::audio::ValidateAudioMixerAsset(*asset).empty();
}

bool EditorSceneAudioSettingsService::SetSceneAudioMixer(kb::assets::AssetId id) {
    if (!executor_ || !IsMixerCandidateValid(id)
        || kb::scene::SceneAudioMixerAccess::ActiveMixer(scene_) == id.value) {
        return false;
    }
    kb::scene::Scene* scene = &scene_;
    return executor_(id.IsValid() ? "Set Scene Audio Mixer" : "Clear Scene Audio Mixer", [scene, id]() {
        kb::scene::SceneAudioMixerAccess::SetActiveMixer(*scene, id.value);
        return true;
    });
}

bool EditorSceneAudioSettingsService::SetSceneAudioSnapshot(std::string_view snapshot) {
    if (!executor_ || kb::scene::SceneAudioMixerAccess::ActiveSnapshot(scene_) == snapshot) {
        return false;
    }
    const std::string value{ snapshot };
    if (value.empty()) {
        kb::scene::Scene* scene = &scene_;
        return executor_("Clear Scene Audio Snapshot", [scene]() {
            return kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(*scene, {});
        });
    }
    const kb::assets::AssetId mixerId{ kb::scene::SceneAudioMixerAccess::ActiveMixer(scene_) };
    if (!mixerId.IsValid()) {
        return false;
    }
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(mixerId);
    const kb::assets::AssetHandle<kb::audio::AudioMixerAsset> mixer =
        metadata != nullptr && metadata->type == kb::audio::kAudioMixerAssetType
        ? manager.Load<kb::audio::AudioMixerAsset>(mixerId)
        : kb::assets::AssetHandle<kb::audio::AudioMixerAsset>{};
    if (!mixer.IsLoaded() || !kb::audio::ValidateAudioMixerAsset(*mixer).empty()
        || (!snapshot.empty() && mixer->FindSnapshot(snapshot) == nullptr)) {
        return false;
    }
    kb::scene::Scene* scene = &scene_;
    return executor_("Set Scene Audio Snapshot", [scene, value]() {
        return kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(*scene, value);
    });
}

bool EditorSceneAudioSettingsService::SetSceneAudioOcclusion(
    const kb::scene::AudioOcclusionSettings& settings) {
    const kb::scene::AudioOcclusionSettings& current =
        kb::scene::SceneAudioOcclusionAccess::Settings(scene_);
    if (!executor_ || !kb::scene::IsAudioOcclusionSettingsValid(settings)
        || (current.enabled == settings.enabled
            && current.occludedVolumeScale == settings.occludedVolumeScale
            && current.maxDistance == settings.maxDistance
            && current.layerMask == settings.layerMask
            && current.maxRaycastsPerTick == settings.maxRaycastsPerTick)) {
        return false;
    }
    kb::scene::Scene* scene = &scene_;
    return executor_("Edit Scene Audio Occlusion", [scene, settings]() {
        return kb::scene::SceneAudioOcclusionAccess::Configure(*scene, settings);
    });
}

void EditorSceneAudioSettingsService::ResetForNewDocument(kb::scene::Scene& scene) noexcept {
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, 0U);
    static_cast<void>(kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(scene, {}));
    kb::scene::SceneAudioMixerAccess::ResetRuntimeMixerState(scene);
    static_cast<void>(kb::scene::SceneAudioOcclusionAccess::Configure(
        scene, kb::scene::AudioOcclusionSettings{}));
}

void EditorSceneAudioSettingsService::PrepareDocument(kb::scene::Scene& scene) {
    const kb::assets::AssetId mixerId{
        kb::scene::SceneAudioMixerAccess::ActiveMixer(scene)
    };
    if (!mixerId.IsValid()) {
        return;
    }
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(mixerId);
    if (metadata != nullptr && metadata->type == kb::audio::kAudioMixerAssetType) {
        static_cast<void>(manager.Load<kb::audio::AudioMixerAsset>(mixerId));
    }
}

} // namespace kb::editor
