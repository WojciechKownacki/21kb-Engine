#pragma once

#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <filesystem>
#include <string>

namespace kb::scene {

class Scene;

struct SceneDocumentLoadResult {
    bool succeeded = false;
    SceneDocument document{};
    std::string error;
};

// LIB-071: the additive counterpart to LoadIntoScene — instantiates
// `document.worldPrefab` into `scene` WITHOUT first clearing existing
// root entities (LoadIntoScene's ClearSceneRoots step), so previously
// loaded content survives. `root` is the new content's own root entity
// (SceneEntity{} / invalid on failure), letting a caller (e.g.
// kb::scene::SceneLoadedContent) track which entities belong to this
// specific load for a later selective Unload.
struct SceneDocumentAdditiveLoadResult {
    bool succeeded = false;
    SceneEntity root{};
};

class SceneDocumentService {
public:
    SceneDocumentService() = delete;

    [[nodiscard]] static SceneDocument Capture(Scene& scene, std::string name);
    [[nodiscard]] static bool Save(Scene& scene, const std::filesystem::path& path, std::string name);
    [[nodiscard]] static bool Save(const SceneDocument& document, const std::filesystem::path& path);
    [[nodiscard]] static SceneDocumentLoadResult Load(const std::filesystem::path& path);
    [[nodiscard]] static bool LoadIntoScene(Scene& scene, const SceneDocument& document);
    [[nodiscard]] static bool LoadFileIntoScene(Scene& scene, const std::filesystem::path& path);
    [[nodiscard]] static SceneDocumentAdditiveLoadResult LoadIntoSceneAdditive(Scene& scene, const SceneDocument& document);
};

} // namespace kb::scene
