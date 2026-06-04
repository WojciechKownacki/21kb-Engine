#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptBackend.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptLifecycle.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace kb::script {

struct ScriptRuntimeDispatchOptions {
    std::size_t maxEventDepth = 8;
};

struct ScriptRuntimeExecutionResult {
    std::size_t visitedBehaviours = 0;
    std::size_t executedBehaviours = 0;
    std::vector<ScriptEvent> emittedEvents;
    std::vector<ScriptDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return diagnostics.empty();
    }
};

class ScriptRuntime final {
public:
    ScriptRuntime() = default;

    [[nodiscard]] bool RegisterBackend(std::unique_ptr<IScriptBackend> backend);
    [[nodiscard]] IScriptBackend* FindBackend(kb::scene::BehaviourBackend backend) noexcept;
    [[nodiscard]] const IScriptBackend* FindBackend(kb::scene::BehaviourBackend backend) const noexcept;

    [[nodiscard]] ScriptRuntimeExecutionResult ExecuteLifecycle(kb::scene::Scene& scene, ScriptLifecycleEvent event, float deltaSeconds);
    [[nodiscard]] ScriptRuntimeExecutionResult DispatchEvent(kb::scene::Scene& scene, const ScriptEvent& event, float deltaSeconds);
    [[nodiscard]] ScriptRuntimeExecutionResult ExecuteLifecycleAndDispatchEvents(
        kb::scene::Scene& scene,
        ScriptLifecycleEvent event,
        float deltaSeconds,
        ScriptRuntimeDispatchOptions options = {});
    [[nodiscard]] ScriptRuntimeExecutionResult DispatchEventAndDrain(
        kb::scene::Scene& scene,
        const ScriptEvent& event,
        float deltaSeconds,
        ScriptRuntimeDispatchOptions options = {});

private:
    [[nodiscard]] static std::size_t BackendIndex(kb::scene::BehaviourBackend backend) noexcept;
    static void MergeResult(ScriptRuntimeExecutionResult& target, ScriptRuntimeExecutionResult source);
    [[nodiscard]] ScriptRuntimeExecutionResult DrainEvents(
        kb::scene::Scene& scene,
        std::vector<ScriptEvent> initialEvents,
        float deltaSeconds,
        ScriptRuntimeDispatchOptions options);

    std::array<std::unique_ptr<IScriptBackend>, 3> backends_{};
};

} // namespace kb::script
