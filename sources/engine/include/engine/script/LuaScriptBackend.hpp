#pragma once

#include "engine/script/ScriptBackend.hpp"

#include <string>
#include <string_view>

namespace kb::script {

struct LuaScriptLoadResult {
    bool succeeded = false;
    std::string error;
};

class ILuaScriptAssetStore {
public:
    virtual ~ILuaScriptAssetStore() = default;

    [[nodiscard]] virtual LuaScriptLoadResult LoadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName = {}) = 0;
    virtual void UnloadScript(kb::assets::AssetId assetId) noexcept = 0;
    virtual void Clear() noexcept = 0;
    [[nodiscard]] virtual bool HasScript(kb::assets::AssetId assetId) const noexcept = 0;
};

class ILuaScriptRuntime {
public:
    virtual ~ILuaScriptRuntime() = default;

    [[nodiscard]] virtual ScriptBackendExecutionResult ExecuteLifecycle(
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context) = 0;
    // LIB-104: eventId accepted for interface parity with IScriptBackend
    // but not used — Lua event dispatch (PucLuaScriptRuntime::ExecuteEvent)
    // resolves by calling the Lua global named `event.name` directly
    // (lua_getglobal), an inherently string-keyed operation of the Lua VM
    // itself; replacing that would mean rearchitecting Lua function
    // dispatch, out of scope here (same deferral as LIB-097's Lua-
    // coroutine finding).
    [[nodiscard]] virtual ScriptBackendExecutionResult ExecuteEvent(
        const kb::scene::BehaviourComponent& behaviour,
        const ScriptEvent& event,
        EventId eventId,
        ScriptExecutionContext& context) = 0;

    // Seed an editor-authored per-instance override of an exposed ("@expose")
    // variable, creating the (entity,asset) instance record if needed and
    // marking it overridden so the per-frame default re-sync preserves it.
    // Default no-op so non-Puc test doubles need not implement it.
    virtual void SetInstanceVariableOverride(
        kb::scene::SceneEntity entity,
        kb::assets::AssetId assetId,
        std::string_view name,
        ScriptValue value) {
        static_cast<void>(entity);
        static_cast<void>(assetId);
        static_cast<void>(name);
        static_cast<void>(value);
    }
};

class LuaScriptBackend final : public IScriptBackend {
public:
    explicit LuaScriptBackend(ILuaScriptRuntime& runtime) noexcept;

    [[nodiscard]] kb::scene::BehaviourBackend Backend() const noexcept override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, EventId eventId, ScriptExecutionContext& context) override;
    void ApplyExposedVariableOverrides(
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        std::span<const kb::scene::BehaviourVariableOverride> overrides) override;

private:
    ILuaScriptRuntime& runtime_;
};

} // namespace kb::script
