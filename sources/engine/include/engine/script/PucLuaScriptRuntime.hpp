#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/script/LuaScriptBackend.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

struct lua_State;

namespace kb::script {

struct PucLuaLoadResult {
    bool succeeded = false;
    std::string error;
};

class PucLuaScriptRuntime final : public ILuaScriptRuntime {
public:
    PucLuaScriptRuntime();
    ~PucLuaScriptRuntime() override;

    PucLuaScriptRuntime(const PucLuaScriptRuntime&) = delete;
    PucLuaScriptRuntime& operator=(const PucLuaScriptRuntime&) = delete;
    PucLuaScriptRuntime(PucLuaScriptRuntime&&) = delete;
    PucLuaScriptRuntime& operator=(PucLuaScriptRuntime&&) = delete;

    [[nodiscard]] PucLuaLoadResult LoadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName = {});
    void UnloadScript(kb::assets::AssetId assetId) noexcept;
    void Clear() noexcept;
    [[nodiscard]] bool HasScript(kb::assets::AssetId assetId) const noexcept;

    [[nodiscard]] ScriptBackendExecutionResult ExecuteLifecycle(
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context) override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteEvent(
        const kb::scene::BehaviourComponent& behaviour,
        const ScriptEvent& event,
        ScriptExecutionContext& context) override;

private:
    [[nodiscard]] ScriptBackendExecutionResult ExecuteFunction(
        const kb::scene::BehaviourComponent& behaviour,
        std::string_view functionName,
        ScriptExecutionContext& context,
        const ScriptEvent* event);
    [[nodiscard]] int FindScriptEnvironment(kb::assets::AssetId assetId) const noexcept;

    lua_State* state_ = nullptr;
    std::unordered_map<std::uint64_t, int> environments_;
};

} // namespace kb::script
