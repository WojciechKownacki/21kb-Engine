#include "engine/input/InputAssetLoaders.hpp"

#include "engine/input/InputAssetIO.hpp"
#include "engine/assets/AssetRegistry.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace kb::input {
namespace {

std::vector<kb::assets::AssetId> ActionDependencies(const InputMappingContextAsset& context) {
    std::vector<kb::assets::AssetId> dependencies;
    const auto add = [&](std::uint64_t id) {
        if (id != 0U) dependencies.push_back(kb::assets::AssetId{id});
    };
    const auto binding = [&](const auto& value) {
        add(value.actionId);
        for (const auto& trigger : value.triggers) {
            if (trigger.type == InputTriggerType::Chorded) add(trigger.chordActionId);
        }
    };
    for (const auto& mapping : context.mappings) binding(mapping);
    for (const auto& composite : context.composites) binding(composite);
    std::ranges::sort(dependencies, {}, &kb::assets::AssetId::value);
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    return dependencies;
}

std::optional<std::string> ValidateActions(const InputAssetLoadResult<InputMappingContextAsset>& loaded,
    const kb::assets::AssetRegistry& registry) {
    if (!loaded.succeeded) return loaded.error;
    for (const auto id : ActionDependencies(loaded.asset)) {
        const auto* action = registry.Find(id);
        if (action == nullptr || action->type != "InputAction") {
            return "Input mapping context references a missing or invalid action: " + kb::assets::ToString(id);
        }
    }
    return std::nullopt;
}

} // namespace

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

std::vector<kb::assets::AssetId> InputMappingContextAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry&) const {
    const auto loaded = ReadInputMappingContext(metadata.physicalPath);
    return loaded.succeeded ? ActionDependencies(loaded.asset) : std::vector<kb::assets::AssetId>{};
}

std::optional<std::string> InputMappingContextAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    return ValidateActions(ReadInputMappingContext(metadata.physicalPath), registry);
}

std::optional<std::string> InputMappingContextAssetLoader::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request, const kb::assets::AssetRegistry& registry) const {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes, error)) return error;
    return ValidateActions(DecodeInputMappingContext(bytes), registry);
}

} // namespace kb::input
