#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptLifecycle.hpp"

#include <span>
#include <string>
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
        const ScriptEvent* incomingEvent = nullptr) noexcept;

    [[nodiscard]] kb::scene::Scene& GetScene() noexcept;
    [[nodiscard]] const kb::scene::Scene& GetScene() const noexcept;
    [[nodiscard]] kb::scene::SceneEntity Self() const noexcept;
    [[nodiscard]] kb::assets::AssetId Asset() const noexcept;
    [[nodiscard]] kb::scene::BehaviourBackend Backend() const noexcept;
    [[nodiscard]] ScriptLifecycleEvent Lifecycle() const noexcept;
    [[nodiscard]] float DeltaSeconds() const noexcept;
    [[nodiscard]] const ScriptEvent* IncomingEvent() const noexcept;
    [[nodiscard]] std::span<const ScriptEventArgument> EventArguments() const noexcept;

    void Emit(std::string eventName);
    void Emit(std::string eventName, std::vector<ScriptEventArgument> arguments);

private:
    kb::scene::Scene& scene_;
    kb::scene::SceneEntity self_{};
    kb::assets::AssetId assetId_{};
    kb::scene::BehaviourBackend backend_ = kb::scene::BehaviourBackend::Native;
    ScriptLifecycleEvent lifecycle_ = ScriptLifecycleEvent::Tick;
    float deltaSeconds_ = 0.0F;
    std::vector<ScriptEvent>* emittedEvents_ = nullptr;
    const ScriptEvent* incomingEvent_ = nullptr;
};

} // namespace kb::script
