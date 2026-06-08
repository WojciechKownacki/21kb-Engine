#include "scene/assets/SceneAssetLoader.hpp"

#include "engine/scene/SceneDocument.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"
#include "scene/asset/io/SceneAssetReader.hpp"

#include <memory>
#include <utility>

namespace kb::scene {

std::string_view SceneAssetLoader::Type() const noexcept {
    return "Scene";
}

std::type_index SceneAssetLoader::PayloadType() const noexcept {
    return typeid(SceneDocument);
}

std::vector<std::string> SceneAssetLoader::Extensions() const {
    return {std::string{SceneAssetFormat::Extension}};
}

kb::assets::AssetLoadResult SceneAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    SceneDocumentLoadResult loaded = SceneAssetReader::Read(request.resolvedPath);
    if (!loaded.succeeded) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = std::move(loaded.error)};
    }

    return kb::assets::AssetLoadResult{.asset = std::make_shared<SceneDocument>(std::move(loaded.document)), .error = {}};
}

} // namespace kb::scene
