#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptExecutionContext.hpp"

#include <string>
#include <vector>

namespace kb::script {

struct ScriptDiagnostic {
    kb::scene::SceneEntity entity{};
    kb::assets::AssetId assetId{};
    kb::scene::BehaviourBackend backend = kb::scene::BehaviourBackend::Native;
    std::string message;
};

struct ScriptBackendExecutionResult {
    bool executed = false;
    std::vector<ScriptDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return diagnostics.empty();
    }
};

class IScriptBackend {
public:
    virtual ~IScriptBackend() = default;

    [[nodiscard]] virtual kb::scene::BehaviourBackend Backend() const noexcept = 0;
    [[nodiscard]] virtual ScriptBackendExecutionResult ExecuteLifecycle(
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context) = 0;
    [[nodiscard]] virtual ScriptBackendExecutionResult ExecuteEvent(
        const kb::scene::BehaviourComponent& behaviour,
        const ScriptEvent& event,
        ScriptExecutionContext& context) = 0;
};

} // namespace kb::script
