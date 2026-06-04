#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptLifecycle.hpp"
#include "engine/script/ScriptSharedState.hpp"

#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::script {

class ScriptExecutionContext final {
public:
    ScriptExecutionContext(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity self,
        kb::assets::AssetId assetId,
        kb::scene::BehaviourBackend backend,
        ScriptLifecycleEvent lifecycle,
        float deltaSeconds,
        std::vector<ScriptEvent>* emittedEvents,
        const ScriptEvent* incomingEvent = nullptr,
        ScriptSharedState* sharedState = nullptr,
        ScriptFunctionRegistry* functions = nullptr) noexcept;

    [[nodiscard]] kb::scene::Scene& GetScene() noexcept;
    [[nodiscard]] const kb::scene::Scene& GetScene() const noexcept;
    [[nodiscard]] kb::scene::SceneEntity Self() const noexcept;
    [[nodiscard]] kb::assets::AssetId Asset() const noexcept;
    [[nodiscard]] kb::scene::BehaviourBackend Backend() const noexcept;
    [[nodiscard]] ScriptLifecycleEvent Lifecycle() const noexcept;
    [[nodiscard]] float DeltaSeconds() const noexcept;
    [[nodiscard]] const ScriptEvent* IncomingEvent() const noexcept;
    [[nodiscard]] std::span<const ScriptEventArgument> EventArguments() const noexcept;
    [[nodiscard]] ScriptSharedState* SharedState() noexcept;
    [[nodiscard]] const ScriptSharedState* SharedState() const noexcept;
    [[nodiscard]] bool SetSharedValue(std::string key, ScriptValue value);
    [[nodiscard]] bool HasSharedValue(std::string_view key) const;
    [[nodiscard]] std::optional<ScriptValue> GetSharedValue(std::string_view key) const;
    [[nodiscard]] bool RemoveSharedValue(std::string_view key);
    [[nodiscard]] ScriptFunctionRegistry* Functions() noexcept;
    [[nodiscard]] const ScriptFunctionRegistry* Functions() const noexcept;
    [[nodiscard]] ScriptFunctionCallResult CallFunction(std::string_view name, std::span<const ScriptFunctionArgument> arguments);

    void Emit(std::string eventName);
    void Emit(std::string eventName, std::vector<ScriptEventArgument> arguments);
    void EmitTo(kb::scene::SceneEntity target, std::string eventName);
    void EmitTo(kb::scene::SceneEntity target, std::string eventName, std::vector<ScriptEventArgument> arguments);

private:
    kb::scene::Scene& scene_;
    kb::scene::SceneEntity self_{};
    kb::assets::AssetId assetId_{};
    kb::scene::BehaviourBackend backend_ = kb::scene::BehaviourBackend::Native;
    ScriptLifecycleEvent lifecycle_ = ScriptLifecycleEvent::Tick;
    float deltaSeconds_ = 0.0F;
    std::vector<ScriptEvent>* emittedEvents_ = nullptr;
    const ScriptEvent* incomingEvent_ = nullptr;
    ScriptSharedState* sharedState_ = nullptr;
    ScriptFunctionRegistry* functions_ = nullptr;
};

} // namespace kb::script
