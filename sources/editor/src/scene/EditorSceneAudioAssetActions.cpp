#include "scene/EditorSceneAudioAssetActions.hpp"

#include "engine/assets/AssetImportTypes.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"

namespace kb::editor {

bool EditorSceneAudioAssetActions::IsAudioAsset(const kb::assets::AssetMetadata& metadata) {
    return metadata.type == "AudioClip" || metadata.importCategory == kb::assets::ToString(kb::assets::AssetImportCategory::Audio);
}

bool EditorSceneAudioAssetActions::AssignAudioClip(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    kb::assets::AssetId clipAssetId) {
    if (!scene.Entities().IsAlive(entity) || !clipAssetId.IsValid()) {
        return false;
    }

    kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity);
    if (source == nullptr) {
        return false;
    }

    source->clipAssetId = clipAssetId.value;
    scene.Components().AudioSources().MarkModified(entity);
    return true;
}

} // namespace kb::editor
