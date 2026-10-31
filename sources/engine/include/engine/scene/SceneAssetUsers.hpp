#pragma once

#include "engine/scene/SceneDocument.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

// One object of a scene that names an asset through one of its components.
struct SceneAssetUser {
    std::uint64_t assetId = 0U;
    std::string objectName;
};

// Every object of the document that names an asset, once per object and asset, in object order.
// It follows the same component fields the scene's dependency sidecar records, so "used by" in a
// tool and what the cooker packages never disagree.
[[nodiscard]] std::vector<SceneAssetUser> CollectSceneAssetUsers(const SceneDocument& document);

} // namespace kb::scene
