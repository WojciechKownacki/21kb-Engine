#include "engine/input/InputAssetLoaders.hpp"

#include "engine/input/InputAssetIO.hpp"

#include <memory>
#include <utility>

namespace kb::input {

std::string_view InputActionAssetLoader::Type() const noexcept {
    return "InputAction";
}

std::type_index InputActionAssetLoader::PayloadType() const noexcept {
    return typeid(InputActionAsset);
}

std::vector<std::string> InputActionAssetLoader::Extensions() const {
    return {std::string{InputAssetFormat::ActionExtension}};
}

kb::assets::AssetLoadResult InputActionAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    InputAssetLoadResult<InputActionAsset> loaded = ReadInputAction(request.resolvedPath);
    if (!loaded.succeeded) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = std::move(loaded.error)};
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<InputActionAsset>(std::move(loaded.asset)), .error = {}};
}

std::string_view InputMappingContextAssetLoader::Type() const noexcept {
    return "InputMappingContext";
}

std::type_index InputMappingContextAssetLoader::PayloadType() const noexcept {
    return typeid(InputMappingContextAsset);
}

std::vector<std::string> InputMappingContextAssetLoader::Extensions() const {
    return {std::string{InputAssetFormat::ContextExtension}};
}

kb::assets::AssetLoadResult InputMappingContextAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    InputAssetLoadResult<InputMappingContextAsset> loaded = ReadInputMappingContext(request.resolvedPath);
    if (!loaded.succeeded) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = std::move(loaded.error)};
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<InputMappingContextAsset>(std::move(loaded.asset)), .error = {}};
}

} // namespace kb::input
