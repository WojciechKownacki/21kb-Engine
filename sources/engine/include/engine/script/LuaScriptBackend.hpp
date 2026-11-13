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

    // See IScriptBackend::HasEntry. Default yes so lightweight test runtimes need not implement it.
    [[nodiscard]] virtual bool HasEntry(
        const kb::scene::BehaviourComponent& behaviour,
        std::string_view name) {
        static_cast<void>(behaviour);
        static_cast<void>(name);
        return true;
    }

    // See IScriptBackend::ResolvePhaseCall: false when the runtime has no resolved call of its own, and the backend then
    // runs the phase through ExecuteLifecycle.
    [[nodiscard]] virtual bool ResolvePhaseCall(
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptLifecycleEvent event,
        ScriptPhaseCall& call) {
        static_cast<void>(entity);
        static_cast<void>(behaviour);
        static_cast<void>(event);
        static_cast<void>(call);
        return false;
    }
    [[nodiscard]] virtual std::uint64_t PhaseCallRevision() const noexcept {
        return 0U;
    }

    // See IScriptBackend::BeginEventBroadcast. Defaults do nothing.
    virtual void BeginEventBroadcast(const ScriptEvent& event) {
        static_cast<void>(event);
    }
    virtual void EndEventBroadcast() noexcept {}

    // Lua implementations with retained event callbacks release every
    // callback owned by this asset before the scene system performs its
    // explicit hot-reload restart. Default no-op keeps lightweight test
    // runtimes and non-retaining implementations source-compatible.
    virtual void ResetAssetForHotReload(
        kb::assets::AssetId assetId,
        ScriptEventBus& events) noexcept {
        static_cast<void>(assetId);
        static_cast<void>(events);
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
    [[nodiscard]] bool HasEntry(const kb::scene::BehaviourComponent& behaviour, std::string_view name) override;
    void BeginEventBroadcast(const ScriptEvent& event) override;
    void EndEventBroadcast() noexcept override;
    [[nodiscard]] ScriptPhaseCall ResolvePhaseCall(
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptLifecycleEvent event) override;
    [[nodiscard]] std::uint64_t PhaseCallRevision() const noexcept override;
    void ResetAssetForHotReload(kb::assets::AssetId assetId, ScriptEventBus& events) noexcept override;

private:
    ILuaScriptRuntime& runtime_;
};

} // namespace kb::script
