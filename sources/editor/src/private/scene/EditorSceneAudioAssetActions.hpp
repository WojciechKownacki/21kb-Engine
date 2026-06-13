#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::editor {

class EditorSceneAudioAssetActions {
public:
    EditorSceneAudioAssetActions() = delete;

    [[nodiscard]] static bool IsAudioAsset(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] static bool AssignAudioClip(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        kb::assets::AssetId clipAssetId);
};

} // namespace kb::editor
