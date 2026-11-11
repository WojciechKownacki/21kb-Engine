#include "engine/render/LightProbeGridAssetLoader.hpp"

#include "engine/render/StaticLightmapBaker.hpp"

#include <memory>
#include <utility>

namespace kb::render {

std::string_view LightProbeGridAssetLoader::Type() const noexcept {
    return kLightProbeGridAssetType;
}

std::type_index LightProbeGridAssetLoader::PayloadType() const noexcept {
    return typeid(LightProbeGrid);
}

std::vector<std::string> LightProbeGridAssetLoader::Extensions() const {
    return { std::string{ kLightProbeGridExtension } };
}

kb::assets::AssetLoadResult LightProbeGridAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes, error)) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }
    LightProbeGrid grid = ParseLightProbeGrid(std::string_view{ reinterpret_cast<const char*>(bytes.data()), bytes.size() });
    if (!grid.Succeeded()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "light probe grid: " + grid.error };
    }
    return kb::assets::AssetLoadResult{ .asset = std::make_shared<LightProbeGrid>(std::move(grid)), .error = {} };
}

} // namespace kb::render
