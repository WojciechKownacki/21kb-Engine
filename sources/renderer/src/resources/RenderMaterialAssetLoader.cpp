#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include "resources/RenderMaterialAssetParser.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace kb::render {

std::string_view RenderMaterialAssetLoader::Type() const noexcept {
    return "RenderMaterial";
}

std::type_index RenderMaterialAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMaterialAssetData);
}

std::vector<std::string> RenderMaterialAssetLoader::Extensions() const {
    return { ".kbmat" };
}

kb::assets::AssetLoadResult RenderMaterialAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::optional<RenderMaterialAssetData> material = LoadMaterial(request.resolvedPath);
    if (!material.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Render material asset load failed" };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMaterialAssetData>(*material),
        .error = {},
    };
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetLoader::LoadMaterial(const std::filesystem::path& path) {
    return RenderMaterialAssetParser::Load(path);
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetLoader::LoadMaterial(std::istream& input) {
    return RenderMaterialAssetParser::Parse(input);
}

} // namespace kb::render
