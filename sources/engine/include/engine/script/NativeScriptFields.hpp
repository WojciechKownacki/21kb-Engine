#pragma once

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/BehaviourVariableOverride.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/script/ScriptExecutionContext.hpp"

#include <optional>
#include <string_view>

namespace kb::script {

// The value of a field the script's .native file declares with `expose = name Type = default`: what was
// set in the Inspector for the object this callback runs on, else the declared default. The values live in
// the scene, so they are the same after a rebuilt module is loaded. Empty when the script declares no
// field of that name.
[[nodiscard]] inline std::optional<ScriptValue> NativeField(ScriptExecutionContext& context, std::string_view name) {
    for (const kb::scene::BehaviourVariableOverride& entry : context.GetScene().Entities().BehaviourVariableOverrides(context.Self())) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    const kb::assets::AssetHandle<NativeBehaviourDescriptor> descriptor =
        context.GetScene().Assets().Manager().Load<NativeBehaviourDescriptor>(context.Asset());
    if (!descriptor.IsLoaded()) {
        return std::nullopt;
    }
    for (std::size_t index = 0U; index < descriptor->exposedVariables.size() && index < descriptor->exposedVariableDefaults.size(); ++index) {
        if (descriptor->exposedVariables[index].name == name) {
            return descriptor->exposedVariableDefaults[index];
        }
    }
    return std::nullopt;
}

} // namespace kb::script
