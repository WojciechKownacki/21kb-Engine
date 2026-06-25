#pragma once

#include "engine/assets/AssetId.hpp"

#include <string>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorMaterialReferenceFinder {
public:
    EditorMaterialReferenceFinder() = delete;

    [[nodiscard]] static std::vector<std::string> FindSceneReferences(const kb::scene::Scene& scene, kb::assets::AssetId materialAssetId);
};

} // namespace kb::editor
