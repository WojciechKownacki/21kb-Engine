#include "engine/script/ScriptBehaviourAsset.hpp"

#include "engine/script/ScriptAsset.hpp"

namespace kb::script {

std::optional<kb::scene::BehaviourBackend> ScriptBehaviourAsset::BackendForAssetType(std::string_view assetType) noexcept {
    if (assetType == ScriptAssetTypes::LuaScript) {
        return kb::scene::BehaviourBackend::Lua;
    }
    if (assetType == ScriptAssetTypes::NativeBehaviour) {
        return kb::scene::BehaviourBackend::Native;
    }
    if (assetType == "VisualGraph") {
        return kb::scene::BehaviourBackend::VisualGraph;
    }
    return std::nullopt;
}

bool ScriptBehaviourAsset::IsBehaviourAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return BackendForAssetType(metadata.type).has_value();
}

std::optional<kb::scene::BehaviourComponent> ScriptBehaviourAsset::CreateComponent(
    const kb::assets::AssetMetadata& metadata,
    bool enabled) noexcept {
    const std::optional<kb::scene::BehaviourBackend> backend = BackendForAssetType(metadata.type);
    if (!backend.has_value() || !metadata.id.IsValid()) {
        return std::nullopt;
    }

    return kb::scene::BehaviourComponent{
        .behaviourAssetId = metadata.id.value,
        .backend = *backend,
        .enabled = enabled,
    };
}

} // namespace kb::script
