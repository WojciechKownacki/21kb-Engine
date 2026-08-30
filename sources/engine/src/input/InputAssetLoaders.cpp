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
    // Input Axis assets reuse the action format + this loader; only the file
    // extension (and the editor's default value type) differ.
    return {std::string{InputAssetFormat::ActionExtension}, std::string{InputAssetFormat::AxisExtension}};
}

kb::assets::AssetLoadResult InputActionAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = std::move(error)};
    }
    InputAssetLoadResult<InputActionAsset> loaded = DecodeInputAction(sourceBytes);
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
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = std::move(error)};
    }
    InputAssetLoadResult<InputMappingContextAsset> loaded = DecodeInputMappingContext(sourceBytes);
    if (!loaded.succeeded) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = std::move(loaded.error)};
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<InputMappingContextAsset>(std::move(loaded.asset)), .error = {}};
}

} // namespace kb::input
