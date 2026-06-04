#include "engine/visual/VisualGraphAssetLoader.hpp"

#include "visual/VisualGraphParser.hpp"

#include <fstream>
#include <memory>

namespace kb::visual {

std::string_view VisualGraphAssetLoader::Type() const noexcept {
    return "VisualGraph";
}

std::type_index VisualGraphAssetLoader::PayloadType() const noexcept {
    return typeid(VisualGraphAsset);
}

std::vector<std::string> VisualGraphAssetLoader::Extensions() const {
    return {".kbgraph", ".flow"};
}

kb::assets::AssetLoadResult VisualGraphAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::ifstream input{request.resolvedPath, std::ios::binary};
    if (!input.is_open()) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = "Visual graph file could not be opened"};
    }

    VisualGraphParseResult parsed = VisualGraphParser::Parse(input);
    if (!parsed.Succeeded()) {
        std::string error = "Visual graph parse failed";
        for (const std::string& entry : parsed.errors) {
            error += "\n";
            error += entry;
        }
        return kb::assets::AssetLoadResult{.asset = {}, .error = std::move(error)};
    }

    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<VisualGraphAsset>(std::move(parsed.graph)),
        .error = {},
    };
}

} // namespace kb::visual
