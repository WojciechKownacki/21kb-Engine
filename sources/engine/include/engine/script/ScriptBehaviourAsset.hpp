#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/BehaviourComponent.hpp"

#include <optional>
#include <string_view>

namespace kb::script {

class ScriptBehaviourAsset {
public:
    ScriptBehaviourAsset() = delete;

    [[nodiscard]] static bool IsBehaviourAsset(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static std::optional<kb::scene::BehaviourBackend> BackendForAssetType(std::string_view assetType) noexcept;
    [[nodiscard]] static std::optional<kb::scene::BehaviourComponent> CreateComponent(
        const kb::assets::AssetMetadata& metadata,
        bool enabled = true) noexcept;
};

} // namespace kb::script
