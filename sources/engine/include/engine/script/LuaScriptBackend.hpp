#pragma once

#include "engine/script/ScriptBackend.hpp"

namespace kb::script {

class ILuaScriptRuntime {
public:
    virtual ~ILuaScriptRuntime() = default;

    [[nodiscard]] virtual ScriptBackendExecutionResult ExecuteLifecycle(
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context) = 0;
    [[nodiscard]] virtual ScriptBackendExecutionResult ExecuteEvent(
        const kb::scene::BehaviourComponent& behaviour,
        const ScriptEvent& event,
        ScriptExecutionContext& context) = 0;
};

class LuaScriptBackend final : public IScriptBackend {
public:
    explicit LuaScriptBackend(ILuaScriptRuntime& runtime) noexcept;

    [[nodiscard]] kb::scene::BehaviourBackend Backend() const noexcept override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, ScriptExecutionContext& context) override;

private:
    ILuaScriptRuntime& runtime_;
};

} // namespace kb::script
