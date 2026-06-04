#pragma once

#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"

namespace kb::script {

class ScriptFunctionVisualGraphBindings final {
public:
    ScriptFunctionVisualGraphBindings() = delete;

    [[nodiscard]] static bool Register(
        kb::visual::VisualGraphRuntimeBindingRegistry& registry,
        const ScriptFunctionRegistry& functions,
        kb::scene::Scene& scene);
    [[nodiscard]] static bool RegisterNative(
        kb::visual::VisualGraphNativeBindingRegistry& registry,
        const ScriptFunctionRegistry& functions);
    [[nodiscard]] static bool RegisterFunction(
        kb::visual::VisualGraphRuntimeBindingRegistry& runtimeBindings,
        kb::visual::VisualGraphNativeBindingRegistry& nativeBindings,
        const ScriptFunctionRegistry& functions,
        const ScriptFunctionSignature& signature,
        kb::scene::Scene& scene);
};

} // namespace kb::script
