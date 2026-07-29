#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/visual/VisualGraphDiagnostic.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeExecutor.hpp"

#include <vector>
#include <string_view>
#include <span>

namespace kb::visual {

struct VisualGraphBehaviourExecutionResult {
    VisualGraphRuntimeExecutionResult runtime;
    std::vector<VisualGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return runtime.Succeeded() && !VisualGraphDiagnostics::HasErrors(diagnostics);
    }
};

class VisualGraphBehaviourRuntime final {
public:
    VisualGraphBehaviourRuntime() = delete;

    [[nodiscard]] static VisualGraphBehaviourExecutionResult Execute(
        const kb::scene::BehaviourComponent& behaviour,
        kb::scene::SceneEntity entity,
        VisualGraphLifecycleEvent event,
        const VisualGraphRuntimeRegistry& artifacts,
        const VisualGraphRuntimeBindingRegistry& bindings,
        VisualGraphRuntimeExecutionContext& context,
        VisualGraphDebugSession* debugger = nullptr);
    [[nodiscard]] static VisualGraphBehaviourExecutionResult ExecuteCustomEvent(
        const kb::scene::BehaviourComponent& behaviour,
        kb::scene::SceneEntity entity,
        std::string_view eventName,
        const VisualGraphRuntimeRegistry& artifacts,
        const VisualGraphRuntimeBindingRegistry& bindings,
        VisualGraphRuntimeExecutionContext& context,
        VisualGraphDebugSession* debugger = nullptr);
    [[nodiscard]] static VisualGraphBehaviourExecutionResult ExecuteCustomEvent(
        const kb::scene::BehaviourComponent& behaviour,
        kb::scene::SceneEntity entity,
        std::string_view eventName,
        std::span<const VisualGraphCustomEventArgument> arguments,
        const VisualGraphRuntimeRegistry& artifacts,
        const VisualGraphRuntimeBindingRegistry& bindings,
        VisualGraphRuntimeExecutionContext& context,
        VisualGraphDebugSession* debugger = nullptr);
};

} // namespace kb::visual
