#include "engine/visual/VisualGraphAssetLoader.hpp"

#include "engine/assets/AssetMemoryInputStream.hpp"
#include "visual/VisualGraphParser.hpp"

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
    std::vector<std::uint8_t> sourceBytes;
    std::string readError;
    if (!request.ReadSourceBytes(sourceBytes, readError)) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = std::move(readError)};
    }
    kb::assets::AssetMemoryInputStream input{sourceBytes};

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
