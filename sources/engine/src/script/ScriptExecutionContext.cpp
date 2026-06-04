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
    const ScriptEvent* incomingEvent,
    ScriptSharedState* sharedState,
    ScriptFunctionRegistry* functions) noexcept
    : scene_(scene)
    , self_(self)
    , assetId_(assetId)
    , backend_(backend)
    , lifecycle_(lifecycle)
    , deltaSeconds_(deltaSeconds)
    , emittedEvents_(emittedEvents)
    , incomingEvent_(incomingEvent)
    , sharedState_(sharedState)
    , functions_(functions) {}

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

ScriptSharedState* ScriptExecutionContext::SharedState() noexcept {
    return sharedState_;
}

const ScriptSharedState* ScriptExecutionContext::SharedState() const noexcept {
    return sharedState_;
}

bool ScriptExecutionContext::SetSharedValue(std::string key, ScriptValue value) {
    return sharedState_ != nullptr && sharedState_->Set(std::move(key), std::move(value));
}

bool ScriptExecutionContext::HasSharedValue(std::string_view key) const {
    return sharedState_ != nullptr && sharedState_->Has(key);
}

std::optional<ScriptValue> ScriptExecutionContext::GetSharedValue(std::string_view key) const {
    return sharedState_ == nullptr ? std::nullopt : sharedState_->Get(key);
}

bool ScriptExecutionContext::RemoveSharedValue(std::string_view key) {
    return sharedState_ != nullptr && sharedState_->Remove(key);
}

ScriptFunctionRegistry* ScriptExecutionContext::Functions() noexcept {
    return functions_;
}

const ScriptFunctionRegistry* ScriptExecutionContext::Functions() const noexcept {
    return functions_;
}

ScriptFunctionCallResult ScriptExecutionContext::CallFunction(std::string_view name, std::span<const ScriptFunctionArgument> arguments) {
    if (functions_ == nullptr) {
        return ScriptFunctionCallResult{
            .errors = {"script function registry is not available"},
        };
    }
    return functions_->Call(name, arguments, ScriptFunctionCallContext{
                                      .scene = &scene_,
                                      .caller = self_,
                                      .callerAsset = assetId_,
                                      .callerBackend = backend_,
                                      .lifecycle = lifecycle_,
                                      .deltaSeconds = deltaSeconds_,
                                  });
}

void ScriptExecutionContext::Emit(std::string eventName) {
    Emit(std::move(eventName), {});
}

void ScriptExecutionContext::Emit(std::string eventName, std::vector<ScriptEventArgument> arguments) {
    EmitTo(kb::scene::SceneEntity{}, std::move(eventName), std::move(arguments));
}

void ScriptExecutionContext::EmitTo(kb::scene::SceneEntity target, std::string eventName) {
    EmitTo(target, std::move(eventName), {});
}

void ScriptExecutionContext::EmitTo(kb::scene::SceneEntity target, std::string eventName, std::vector<ScriptEventArgument> arguments) {
    if (emittedEvents_ == nullptr || eventName.empty()) {
        return;
    }
    emittedEvents_->push_back(ScriptEvent{
        .name = std::move(eventName),
        .sender = self_,
        .target = target,
        .senderAsset = assetId_,
        .arguments = std::move(arguments),
    });
}

} // namespace kb::script
