#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/visual/VisualGraphBehaviourInstanceRegistry.hpp"
#include "engine/visual/VisualGraphBehaviourRuntime.hpp"
#include "engine/visual/VisualGraphDiagnostic.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::visual {

struct VisualGraphBehaviourEmittedEvent {
    kb::scene::SceneEntity sender{};
    kb::scene::SceneEntity target{};
    kb::assets::AssetId assetId{};
    std::string name;
    std::vector<VisualGraphEventArgument> arguments;
};

struct VisualGraphBehaviourLifecycleResult {
    std::size_t visitedBehaviours = 0;
    std::size_t executedBehaviours = 0;
    std::vector<VisualGraphBehaviourEmittedEvent> emittedEvents;
    std::vector<VisualGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return !VisualGraphDiagnostics::HasErrors(diagnostics);
    }
};

class VisualGraphBehaviourLifecycleRunner final {
public:
    VisualGraphBehaviourLifecycleRunner() = delete;

    [[nodiscard]] static VisualGraphBehaviourLifecycleResult Execute(
        kb::scene::Scene& scene,
        VisualGraphLifecycleEvent event,
        const VisualGraphRuntimeRegistry& artifacts,
        const VisualGraphRuntimeBindingRegistry& bindings,
        VisualGraphBehaviourInstanceRegistry& instances);
    [[nodiscard]] static VisualGraphBehaviourLifecycleResult ExecuteCustomEvent(
        kb::scene::Scene& scene,
        std::string_view eventName,
        const VisualGraphRuntimeRegistry& artifacts,
        const VisualGraphRuntimeBindingRegistry& bindings,
        VisualGraphBehaviourInstanceRegistry& instances);
    [[nodiscard]] static VisualGraphBehaviourLifecycleResult ExecuteCustomEvent(
        kb::scene::Scene& scene,
        std::string_view eventName,
        std::span<const VisualGraphCustomEventArgument> arguments,
        const VisualGraphRuntimeRegistry& artifacts,
        const VisualGraphRuntimeBindingRegistry& bindings,
        VisualGraphBehaviourInstanceRegistry& instances);
};

} // namespace kb::visual
