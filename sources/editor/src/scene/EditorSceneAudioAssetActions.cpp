#include "scene/EditorSceneAudioAssetActions.hpp"

#include "engine/assets/AssetKind.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"

namespace kb::editor {

bool EditorSceneAudioAssetActions::IsAudioAsset(const kb::assets::AssetMetadata& metadata) {
    return kb::assets::AssetMatchesKind(metadata, kb::assets::AssetKind::Audio);
}

bool EditorSceneAudioAssetActions::AssignAudioClip(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    kb::assets::AssetId clipAssetId) {
    if (!scene.Entities().IsAlive(entity)) {
        return false;
    }

    if (clipAssetId.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(clipAssetId);
        if (metadata == nullptr || !IsAudioAsset(*metadata)) {
            return false;
        }
    }

    kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity);
    if (source == nullptr) {
        return false;
    }
    if (source->clipAssetId == clipAssetId.value) {
        return false;
    }

    source->clipAssetId = clipAssetId.value;
    scene.Components().AudioSources().MarkModified(entity);
    return true;
}

} // namespace kb::editor
