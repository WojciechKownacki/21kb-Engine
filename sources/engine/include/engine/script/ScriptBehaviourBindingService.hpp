#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptRuntimeAssetPreparer.hpp"

#include <optional>
#include <string>
#include <vector>

namespace kb::script {

struct ScriptBehaviourBindingOptions {
    bool enabled = true;
    kb::scene::BehaviourTickGroup tickGroup = kb::scene::BehaviourTickGroup::Gameplay;
    std::int32_t executionOrder = 0;
    bool prepareRuntimeAsset = true;
};

struct ScriptBehaviourBindingResult {
    bool bound = false;
    std::optional<kb::scene::BehaviourComponent> component;
    ScriptRuntimeAssetPrepareResult prepareResult;
    std::vector<std::string> errors;

    [[nodiscard]] bool Succeeded() const noexcept {
        return bound && errors.empty() && prepareResult.Succeeded();
    }
};

class ScriptBehaviourBindingService final {
public:
    ScriptBehaviourBindingService() = delete;

    [[nodiscard]] static ScriptBehaviourBindingResult AttachAsset(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        kb::assets::AssetId assetId,
        const ScriptBehaviourBindingOptions& options = {},
        ScriptRuntimeAssetPreparer* preparer = nullptr);

    [[nodiscard]] static ScriptBehaviourBindingResult AttachMetadata(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const kb::assets::AssetMetadata& metadata,
        const ScriptBehaviourBindingOptions& options = {},
        ScriptRuntimeAssetPreparer* preparer = nullptr);
};

} // namespace kb::script
