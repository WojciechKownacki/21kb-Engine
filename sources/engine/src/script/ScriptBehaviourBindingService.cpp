#include "engine/script/ScriptBehaviourBindingService.hpp"

#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"

#include <utility>

namespace kb::script {
namespace {

void AddError(ScriptBehaviourBindingResult& result, std::string message) {
    result.errors.push_back(std::move(message));
}

[[nodiscard]] bool HasPrepareDiagnostics(const ScriptRuntimeAssetPrepareResult& result) noexcept {
    return !result.Succeeded();
}

} // namespace

ScriptBehaviourBindingResult ScriptBehaviourBindingService::AttachAsset(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    kb::assets::AssetId assetId,
    const ScriptBehaviourBindingOptions& options,
    ScriptRuntimeAssetPreparer* preparer) {
    ScriptBehaviourBindingResult result{};
    if (!entity.IsValid()) {
        AddError(result, "script behaviour binding target entity is invalid");
        return result;
    }
    if (!assetId.IsValid()) {
        AddError(result, "script behaviour binding asset id is invalid");
        return result;
    }

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr) {
        AddError(result, "script behaviour binding asset is not registered");
        return result;
    }
    return AttachMetadata(scene, entity, *metadata, options, preparer);
}

ScriptBehaviourBindingResult ScriptBehaviourBindingService::AttachMetadata(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const kb::assets::AssetMetadata& metadata,
    const ScriptBehaviourBindingOptions& options,
    ScriptRuntimeAssetPreparer* preparer) {
    ScriptBehaviourBindingResult result{};
    if (!entity.IsValid()) {
        AddError(result, "script behaviour binding target entity is invalid");
        return result;
    }

    std::optional<kb::scene::BehaviourComponent> component = ScriptBehaviourAsset::CreateComponent(metadata, options.enabled);
    if (!component.has_value()) {
        AddError(result, "asset is not a script behaviour asset");
        return result;
    }

    component->tickGroup = options.tickGroup;
    component->executionOrder = options.executionOrder;
    result.component = *component;

    if (options.prepareRuntimeAsset && preparer != nullptr && component->enabled) {
        result.prepareResult = preparer->PrepareBehaviour(*component);
        if (HasPrepareDiagnostics(result.prepareResult)) {
            AddError(result, "script behaviour runtime asset could not be prepared");
            return result;
        }
    }

    scene.Components().Behaviours().Set(entity, *component);
    result.bound = true;
    return result;
}

} // namespace kb::script
