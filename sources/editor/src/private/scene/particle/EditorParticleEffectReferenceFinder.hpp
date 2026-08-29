#pragma once

#include "engine/assets/AssetId.hpp"

#include <string>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorParticleEffectReferenceFinder {
public:
    EditorParticleEffectReferenceFinder() = delete;

    [[nodiscard]] static std::vector<std::string> FindSceneReferences(const kb::scene::Scene& scene, kb::assets::AssetId effectAssetId);
};

} // namespace kb::editor
