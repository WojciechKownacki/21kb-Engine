#pragma once

#include "engine/script/ScriptSharedState.hpp"
#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"

namespace kb::script {

class ScriptSharedVisualGraphBindings final {
public:
    ScriptSharedVisualGraphBindings() = delete;

    [[nodiscard]] static bool Register(kb::visual::VisualGraphRuntimeBindingRegistry& registry, ScriptSharedState& sharedState);
    [[nodiscard]] static bool RegisterNative(kb::visual::VisualGraphNativeBindingRegistry& registry);
};

} // namespace kb::script
