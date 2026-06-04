#include "engine/script/ScriptExecutionContext.hpp"

#include <utility>

namespace kb::script {

ScriptExecutionContext::ScriptExecutionContext(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity self,
    kb::assets::AssetId assetId,
    kb::scene::BehaviourBackend backend,
    ScriptLifecycleEvent lifecycle,
    float deltaSeconds,
    std::vector<ScriptEvent>* emittedEvents,
    const ScriptEvent* incomingEvent) noexcept
    : scene_(scene)
    , self_(self)
    , assetId_(assetId)
    , backend_(backend)
    , lifecycle_(lifecycle)
    , deltaSeconds_(deltaSeconds)
    , emittedEvents_(emittedEvents)
    , incomingEvent_(incomingEvent) {}

kb::scene::Scene& ScriptExecutionContext::GetScene() noexcept {
    return scene_;
}

const kb::scene::Scene& ScriptExecutionContext::GetScene() const noexcept {
    return scene_;
}

kb::scene::SceneEntity ScriptExecutionContext::Self() const noexcept {
    return self_;
}

kb::assets::AssetId ScriptExecutionContext::Asset() const noexcept {
    return assetId_;
}

kb::scene::BehaviourBackend ScriptExecutionContext::Backend() const noexcept {
    return backend_;
}

ScriptLifecycleEvent ScriptExecutionContext::Lifecycle() const noexcept {
    return lifecycle_;
}

float ScriptExecutionContext::DeltaSeconds() const noexcept {
    return deltaSeconds_;
}

const ScriptEvent* ScriptExecutionContext::IncomingEvent() const noexcept {
    return incomingEvent_;
}

std::span<const ScriptEventArgument> ScriptExecutionContext::EventArguments() const noexcept {
    return incomingEvent_ == nullptr ? std::span<const ScriptEventArgument>{} : std::span<const ScriptEventArgument>{incomingEvent_->arguments};
}

void ScriptExecutionContext::Emit(std::string eventName) {
    Emit(std::move(eventName), {});
}

void ScriptExecutionContext::Emit(std::string eventName, std::vector<ScriptEventArgument> arguments) {
    if (emittedEvents_ == nullptr || eventName.empty()) {
        return;
    }
    emittedEvents_->push_back(ScriptEvent{
        .name = std::move(eventName),
        .sender = self_,
        .senderAsset = assetId_,
        .arguments = std::move(arguments),
    });
}

} // namespace kb::script
